
#include "cpa.h"
#include "cpa_cy_im.h"
#include "cpa_cy_ln.h"
#include "icp_sal_poll.h"

#include "cpa_sample_utils.h"

#include "he_qat_types.h"
#include "he_qat_bn_ops.h"

#ifdef HE_QAT_PERF
#include <sys/time.h>
struct timeval start_time, end_time;
double time_taken = 0.0;
#endif

#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <openssl/bn.h>

#ifdef HE_QAT_SYNC_MODE
#pragma message "Synchronous execution mode."
#else
#pragma message "Asynchronous execution mode."
#endif

//#define RESTART_LATENCY_MICROSEC 600
//#define NUM_PKE_SLICES 6

#include "he_qat_gconst.h"

// Global buffer for the runtime environment
HE_QAT_RequestBuffer he_qat_buffer;
HE_QAT_OutstandingBuffer outstanding;

volatile unsigned long response_count = 0;
static volatile unsigned long request_count = 0;
static unsigned long request_latency = 0; // unused
static unsigned long restart_threshold = NUM_PKE_SLICES;//48; 
static unsigned long max_pending = (NUM_PKE_SLICES * 2 * HE_QAT_NUM_ACTIVE_INSTANCES); 

// Callback functions
//extern void HE_QAT_BIGNUMModExpCallback(void* pCallbackTag, CpaStatus status, void* pOpData, CpaFlatBuffer* pOut);
//extern void HE_QAT_bnModExpCallback(void* pCallbackTag, CpaStatus status, void* pOpData, CpaFlatBuffer* pOut);


/// @brief
/// @function
/// Thread-safe producer implementation for the shared request buffer.
/// Stores requests in a buffer that will be offload to QAT devices.
void submit_request(HE_QAT_RequestBuffer* _buffer, void* args) {
#ifdef HE_QAT_DEBUG
    printf("Lock write request\n");
#endif
    pthread_mutex_lock(&_buffer->mutex);

#ifdef HE_QAT_DEBUG
    printf("Wait lock write request. [buffer size: %d]\n", _buffer->count);
#endif
    while (_buffer->count >= HE_QAT_BUFFER_SIZE)
        pthread_cond_wait(&_buffer->any_free_slot, &_buffer->mutex);

    assert(_buffer->count < HE_QAT_BUFFER_SIZE);

    _buffer->data[_buffer->next_free_slot++] = args;

    _buffer->next_free_slot %= HE_QAT_BUFFER_SIZE;
    _buffer->count++;

    pthread_cond_signal(&_buffer->any_more_data);
    pthread_mutex_unlock(&_buffer->mutex);
#ifdef HE_QAT_DEBUG
    printf("Unlocked write request. [buffer size: %d]\n", _buffer->count);
#endif
}

static void submit_request_list(HE_QAT_RequestBuffer* _buffer,
                                HE_QAT_TaskRequestList* _requests) {
//#define HE_QAT_DEBUG
#ifdef HE_QAT_DEBUG
//    printf("Lock submit request list\n");
#endif
    if (0 == _requests->count) return;

    pthread_mutex_lock(&_buffer->mutex);

#ifdef HE_QAT_DEBUG
    printf(
        "Wait lock submit request list. [internal buffer size: %d] [num "
        "requests: %u]\n",
        _buffer->count, _requests->count);
#endif

    // Wait until buffer can accomodate the number of input requests
    while (_buffer->count >= HE_QAT_BUFFER_SIZE ||
           (HE_QAT_BUFFER_SIZE - _buffer->count) < _requests->count)
        pthread_cond_wait(&_buffer->any_free_slot, &_buffer->mutex);

    assert(_buffer->count < HE_QAT_BUFFER_SIZE);
    assert(_requests->count <= (HE_QAT_BUFFER_SIZE - _buffer->count));

    for (unsigned int i = 0; i < _requests->count; i++) {
        _buffer->data[_buffer->next_free_slot++] = _requests->request[i];
        _buffer->next_free_slot %= HE_QAT_BUFFER_SIZE;
        _requests->request[i] = NULL;
    }
    _buffer->count += _requests->count;
    _requests->count = 0;

    pthread_cond_signal(&_buffer->any_more_data);
    pthread_mutex_unlock(&_buffer->mutex);
#ifdef HE_QAT_DEBUG
    printf("Unlocked submit request list. [internal buffer size: %d]\n",
           _buffer->count);
#endif
}

/// @brief
/// @function
/// Thread-safe producer implementation for the shared outstanding request
/// buffer that stores request from multiple threads.
/// Stores requests in a buffer that will be sent to the HE QAT buffer.
/// @unused
static void push_request(HE_QAT_RequestBufferList* _outstanding_buffer,
                         void* args, unsigned int num_requests) {
#ifdef HE_QAT_DEBUG
    printf("Lock write outstanding requests\n");
#endif
    pthread_mutex_lock(&_outstanding_buffer->mutex);

#ifdef HE_QAT_DEBUG
    printf("Wait lock write request. [outstanding buffer size: %d]\n",
           _outstanding_buffer->count);
#endif
    // if (NULL == args) pthread_mutex_unlock(&_outstanding_buffer->mutex);
    unsigned int list_size = _outstanding_buffer->size;
    unsigned int buffer_size = list_size * HE_QAT_BUFFER_SIZE;
    // TODO(fdiasmor): Dynamically expand the outstanding buffer
    //    while (buffer_size < num_requests &&
    //		    buffer_size < HE_QAT_LIST_SIZE * HE_QAT_BUFFER_SIZE) {
    //	_outstanding_buffer->data[list_size] =
    // malloc(sizeof(HE_QAT_TaskRequest)*HE_QAT_BUFFER_SIZE); 	if
    //(_outstanding_buffer)
    //        buffer_size = ++list_size * HE_QAT_BUFFER_SIZE;
    //    }
    // Create more space, if required, to a certain extent
    // For now, it assumes maximum number of requests per thread and per call is
    // equal to HE_QAT_BUFFER_SIZE and maximum number of threads is
    // HE_QAT_BUFFER_COUNT
    while (_outstanding_buffer->count >= buffer_size ||
           (buffer_size - _outstanding_buffer->count + 1 < num_requests))
        pthread_cond_wait(&_outstanding_buffer->any_free_slot,
                          &_outstanding_buffer->mutex);

    assert(_outstanding_buffer->count < buffer_size);
    assert(buffer_size - _outstanding_buffer->count + 1 >= num_requests);

    HE_QAT_TaskRequestList* requests = (HE_QAT_TaskRequestList*)args;
    for (unsigned int i = 0; i < requests->count; i++) {
        unsigned int index = _outstanding_buffer->next_free_slot / buffer_size;
        unsigned int slot = _outstanding_buffer->next_free_slot % buffer_size;
        _outstanding_buffer->data[index][slot] = requests->request[i];
        _outstanding_buffer->next_free_slot++;
        _outstanding_buffer->next_free_slot %= buffer_size;
        _outstanding_buffer->count++;
    }

    pthread_cond_signal(&_outstanding_buffer->any_more_data);
    pthread_mutex_unlock(&_outstanding_buffer->mutex);
#ifdef HE_QAT_DEBUG
    printf("Unlocked write request. [outstanding buffer count: %d]\n",
           _outstanding_buffer->count);
#endif
}

/// @brief
/// @function
/// Thread-safe consumer implementation for the shared request buffer.
/// Read requests from a buffer to finally offload the work to QAT devices.
/// Supported in single-threaded or multi-threaded mode.
static HE_QAT_TaskRequest* read_request(HE_QAT_RequestBuffer* _buffer) {
    void* item = NULL;
    static unsigned int counter = 0;
    pthread_mutex_lock(&_buffer->mutex);
//#define HE_QAT_DEBUG
#ifdef HE_QAT_DEBUG
    printf("Wait lock read request. [internal buffer size: %d] Request #%u\n",
           _buffer->count, counter++);
#endif
    // Wait while buffer is empty
    while (_buffer->count <= 0)
        pthread_cond_wait(&_buffer->any_more_data, &_buffer->mutex);

    assert(_buffer->count > 0);

    item = _buffer->data[_buffer->next_data_slot++];

    _buffer->next_data_slot %= HE_QAT_BUFFER_SIZE;
    _buffer->count--;

    pthread_cond_signal(&_buffer->any_free_slot);
    pthread_mutex_unlock(&_buffer->mutex);
#ifdef HE_QAT_DEBUG
    printf("Unlocked read request. [internal buffer count: %d]\n",
           _buffer->count);
#endif

    return (HE_QAT_TaskRequest*)(item);
}

/// @brief
/// @function
/// Thread-safe consumer implementation for the shared request buffer.
/// Read requests from a buffer to finally offload the work to QAT devices.
/// @future: Meant for multi-threaded mode.
static void read_request_list(HE_QAT_TaskRequestList* _requests,
                              HE_QAT_RequestBuffer* _buffer, unsigned int max_requests) {
    if (NULL == _requests) return;

    pthread_mutex_lock(&_buffer->mutex);

    // Wait while buffer is empty
    while (_buffer->count <= 0)
        pthread_cond_wait(&_buffer->any_more_data, &_buffer->mutex);

    assert(_buffer->count > 0);
    // assert(_buffer->count <= HE_QAT_BUFFER_SIZE);

    unsigned int count = (_buffer->count < max_requests) ? _buffer->count : max_requests;

    //for (unsigned int i = 0; i < _buffer->count; i++) {
    for (unsigned int i = 0; i < count; i++) {
        _requests->request[i] = _buffer->data[_buffer->next_data_slot++];
        _buffer->next_data_slot %= HE_QAT_BUFFER_SIZE;
    }
    //_requests->count = _buffer->count;
    //_buffer->count = 0;
    _requests->count = count;
    _buffer->count -= count;

    pthread_cond_signal(&_buffer->any_free_slot);
    pthread_mutex_unlock(&_buffer->mutex);

    return;
}

/// @brief
/// @function
/// Thread-safe consumer implementation for the shared request buffer.
/// Read requests from a buffer to finally offload the work to QAT devices.
/// @deprecated
//[[deprecated("Replaced by pull_outstanding_requests() in schedule_requests().")]]
static void pull_request(HE_QAT_TaskRequestList* _requests,
                         // HE_QAT_OutstandingBuffer *_outstanding_buffer,
                         HE_QAT_RequestBufferList* _outstanding_buffer,
                         unsigned int max_num_requests) {
    if (NULL == _requests) return;

    pthread_mutex_lock(&_outstanding_buffer->mutex);

    unsigned int list_size = _outstanding_buffer->size;
    unsigned int buffer_size = list_size * HE_QAT_BUFFER_SIZE;

    // Wait while buffer is empty
    while (_outstanding_buffer->count <= 0)
        pthread_cond_wait(&_outstanding_buffer->any_more_data,
                          &_outstanding_buffer->mutex);

    assert(_outstanding_buffer->count > 0);

    unsigned int num_requests = (_outstanding_buffer->count <= max_num_requests)
                                    ? _outstanding_buffer->count
                                    : max_num_requests;

    assert(num_requests <= HE_QAT_BUFFER_SIZE);

    //_requests->count = 0;
    for (unsigned int i = 0; i < num_requests; i++) {
        unsigned int index = _outstanding_buffer->next_data_slot / buffer_size;
        unsigned int slot = _outstanding_buffer->next_data_slot % buffer_size;

        _requests->request[i] = _outstanding_buffer->data[index][slot];
        //_requests->count++;

        _outstanding_buffer->next_data_slot++;
        _outstanding_buffer->next_data_slot %= buffer_size;
        //_outstanding_buffer->count--;
    }
    _requests->count = num_requests;
    _outstanding_buffer->count -= num_requests;

    pthread_cond_signal(&_outstanding_buffer->any_free_slot);
    pthread_mutex_unlock(&_outstanding_buffer->mutex);

    return;
}

static void pull_outstanding_requests(
    HE_QAT_TaskRequestList* _requests,
    HE_QAT_OutstandingBuffer* _outstanding_buffer,
    unsigned int max_num_requests) {
    if (NULL == _requests) return;
    _requests->count = 0;

    // for now, only one thread can change next_ready_buffer
    // so no need for sync tools

    // Select an outstanding buffer to pull requests and add them into the
    // processing queue (internal buffer)
    pthread_mutex_lock(&_outstanding_buffer->mutex);
    // Wait until next outstanding buffer becomes available for use
    while (outstanding.busy_count <= 0)
        pthread_cond_wait(&_outstanding_buffer->any_ready_buffer,
                          &_outstanding_buffer->mutex);

    int any_ready = 0;
    unsigned int index = _outstanding_buffer->next_ready_buffer;  // no fairness
    for (unsigned int i = 0; i < HE_QAT_BUFFER_COUNT; i++) {
        index = i;  // ensure fairness
        if (_outstanding_buffer->ready_buffer[index] &&
            _outstanding_buffer->buffer[index]
                .count) {  // sync with mutex at interface
            any_ready = 1;
            break;
        }
        // index = (index + 1) % HE_QAT_BUFFER_COUNT;
    }
    // Ensures it gets picked once only
    pthread_mutex_unlock(&_outstanding_buffer->mutex);

    if (!any_ready) return;

    // printf("Buffer #%u is Ready\n",index);

    // Extract outstanding requests from outstanding buffer
    // (this is the only function that reads from outstanding buffer, from a
    // single thread)
    pthread_mutex_lock(&_outstanding_buffer->buffer[index].mutex);
    // This conditional waiting may not be required
    // Wait while buffer is empty
    while (_outstanding_buffer->buffer[index].count <= 0) {
        pthread_cond_wait(&_outstanding_buffer->buffer[index].any_more_data,
                          &_outstanding_buffer->buffer[index].mutex);
    }
    assert(_outstanding_buffer->buffer[index].count > 0);
    //

    unsigned int num_requests =
        (_outstanding_buffer->buffer[index].count < max_num_requests)
            ? _outstanding_buffer->buffer[index].count
            : max_num_requests;

    assert(num_requests <= HE_QAT_BUFFER_SIZE);

    for (unsigned int i = 0; i < num_requests; i++) {
        _requests->request[i] =
            _outstanding_buffer->buffer[index]
                .data[_outstanding_buffer->buffer[index].next_data_slot];
        _outstanding_buffer->buffer[index].count--;
        _outstanding_buffer->buffer[index].next_data_slot++;
        _outstanding_buffer->buffer[index].next_data_slot %= HE_QAT_BUFFER_SIZE;
    }
    _requests->count = num_requests;

    pthread_cond_signal(&_outstanding_buffer->buffer[index].any_free_slot);
    pthread_mutex_unlock(&_outstanding_buffer->buffer[index].mutex);

    // ---------------------------------------------------------------------------
    // Notify there is an outstanding buffer in ready for the processing queue
    //    pthread_mutex_lock(&_outstanding_buffer->mutex);
    //
    //    _outstanding_buffer->ready_count--;
    //    _outstanding_buffer->ready_buffer[index] = 0;
    //
    //    pthread_cond_signal(&_outstanding_buffer->any_free_buffer);
    //    pthread_mutex_unlock(&_outstanding_buffer->mutex);

    return;
}

/// @brief
///  Schedule outstanding requests from outstanding buffers to the internal buffer 
///  from which requests are ready to be submitted to the device for processing.
/// @function schedule_requests
/// @param[in] state normally an volatile integer variable to activates(val>0) and disactives(0) the scheduler.
void* schedule_requests(void* state) {
    if (NULL == state) {
        printf("Failed at buffer_manager: argument is NULL.\n");
        pthread_exit(NULL);
    }

    int* active = (int*)state;

    HE_QAT_TaskRequestList outstanding_requests;
    for (unsigned int i = 0; i < HE_QAT_BUFFER_SIZE; i++) {
        outstanding_requests.request[i] = NULL;
    }
    outstanding_requests.count = 0;

    // this thread should receive signal from context to exit
    while (*active) {
        // collect a set of requests from the outstanding buffer
        pull_outstanding_requests(&outstanding_requests, &outstanding,
                                  HE_QAT_BUFFER_SIZE);
        //	printf("Pulled %u outstanding
        //requests\n",outstanding_requests.count);
        // submit them to the HE QAT buffer for offloading
        submit_request_list(&he_qat_buffer, &outstanding_requests);
        //	printf("Submitted %u outstanding
        //requests\n",outstanding_requests.count);
    }

    pthread_exit(NULL);
}

/// @brief
/// @function start_inst_polling
/// @param[in] HE_QAT_InstConfig Parameter values to start and poll instances.
///
static void* start_inst_polling(void* _inst_config) {
    if (NULL == _inst_config) {
        printf(
            "Failed at start_inst_polling: argument is NULL.\n");  //,__FUNC__);
        pthread_exit(NULL);
    }

    HE_QAT_InstConfig* config = (HE_QAT_InstConfig*)_inst_config;

    if (NULL == config->inst_handle) return NULL;

#ifdef HE_QAT_DEBUG
    printf("Instance ID %d Polling\n",config->inst_id);
#endif

    // What is harmful for polling without performing any operation?
    config->polling = 1;
    while (config->polling) {
        icp_sal_CyPollInstance(config->inst_handle, 0);
        OS_SLEEP(50);
    }

    pthread_exit(NULL);
}



void* start_instances(void* _config) {
//    static unsigned int request_count = 0;
    static unsigned int instance_count = 0;
    static unsigned int next_instance = 0;
    
    if (NULL == _config) {
        printf("Failed in start_instances: _config is NULL.\n");
        pthread_exit(NULL);
    }

    HE_QAT_Config* config = (HE_QAT_Config*)_config;
    instance_count = config->count;

    printf("Instance Count: %d\n",instance_count);
    pthread_t* polling_thread = (pthread_t *) malloc(sizeof(pthread_t)*instance_count);
    if (NULL == polling_thread) {
        printf("Failed in start_instances: polling_thread is NULL.\n");
        pthread_exit(NULL);
    }
    unsigned* request_count_per_instance = (unsigned *) malloc(sizeof(unsigned)*instance_count);
    if (NULL == request_count_per_instance) {
        printf("Failed in start_instances: polling_thread is NULL.\n");
        pthread_exit(NULL);
    }
    for (unsigned i = 0; i < instance_count; i++) {
        request_count_per_instance[i] = 0;
    }

    CpaStatus status = CPA_STATUS_FAIL;

    for (unsigned int j = 0; j < config->count; j++) {	    
        // Start from zero or restart after stop_perform_op
        pthread_mutex_lock(&config->inst_config[j].mutex);
        while (config->inst_config[j].active) 
    	    pthread_cond_wait(&config->inst_config[j].ready, 
    			    	&config->inst_config[j].mutex);
    
        // assert(0 == config->active);
        // assert(NULL == config->inst_handle);
    
        status = cpaCyStartInstance(config->inst_config[j].inst_handle);
        config->inst_config[j].status = status;
        if (CPA_STATUS_SUCCESS == status) {
            printf("Cpa CyInstance has successfully started.\n");
            status =
                cpaCySetAddressTranslation(config->inst_config[j].inst_handle, 
				sampleVirtToPhys);
        }
    
        pthread_cond_signal(&config->inst_config[j].ready);
        pthread_mutex_unlock(&config->inst_config[j].mutex);
    
        if (CPA_STATUS_SUCCESS != status) pthread_exit(NULL);

        printf("Instance ID: %d\n",config->inst_config[j].inst_id);

         // Start QAT instance and start polling
         //pthread_t polling_thread;
         if (pthread_create(&polling_thread[j], config->inst_config[j].attr, start_inst_polling,
                            (void*)&(config->inst_config[j])) != 0) {
             printf("Failed at creating and starting polling thread.\n");
             pthread_exit(NULL);
         }

         if (pthread_detach(polling_thread[j]) != 0) {
             printf("Failed at detaching polling thread.\n");
             pthread_exit(NULL);
         }
	 
	 config->inst_config[j].active = 1;
	 config->inst_config[j].running = 1;
    
    } // for loop
    
    /* NEW CODE */
    HE_QAT_TaskRequestList outstanding_requests;
    for (unsigned int i = 0; i < HE_QAT_BUFFER_SIZE; i++) {
        outstanding_requests.request[i] = NULL;
    }
    outstanding_requests.count = 0;
    /* END NEW CODE */

    config->running = 1;
    config->active = 1;
    while (config->running) {
#ifdef HE_QAT_DEBUG
        printf("Try reading request from buffer. Inst #%d\n", config->inst_id);
#endif
	/* NEW CODE */
	unsigned long pending = request_count - response_count;
	unsigned long available = max_pending - ((pending < max_pending)?pending:max_pending);
#ifdef HE_QAT_DEBUG
	printf("[CHECK] request_count: %lu response_count: %lu pending: %lu available: %lu\n",
			request_count,response_count,pending,available);
#endif
	while (available < restart_threshold) {
#ifdef HE_QAT_DEBUG
	   printf("[WAIT]\n");
#endif
	   // argument passed in microseconds 
	   OS_SLEEP(RESTART_LATENCY_MICROSEC);
           pending = request_count - response_count;
	   available = max_pending - ((pending < max_pending)?pending:max_pending);
//           printf("[CHECK] request_count: %lu response_count: %lu pending: %lu available: %lu\n",
//          			request_count,response_count,pending,available);
	}
#ifdef HE_QAT_DEBUG
	printf("[SUBMIT] request_count: %lu response_count: %lu pending: %lu available: %lu\n",
			request_count,response_count,pending,available);
#endif
	unsigned int max_requests = available;
	// Try consume maximum amount of data from butter to perform requested operation
        read_request_list(&outstanding_requests, &he_qat_buffer, max_requests);
	/* END NEW CODE  */

//	// Try consume data from butter to perform requested operation
//        HE_QAT_TaskRequest* request =
//            (HE_QAT_TaskRequest*)read_request(&he_qat_buffer);
//
//        if (NULL == request) {
//            pthread_cond_signal(&config->ready);
//            continue;
//        }
#ifdef HE_QAT_DEBUG
        printf("Offloading %u requests to the accelerator.\n", outstanding_requests.count);
#endif
        for (unsigned int i = 0; i < outstanding_requests.count; i++) {
	   HE_QAT_TaskRequest* request = outstanding_requests.request[i];
#ifdef HE_QAT_SYNC_MODE
        COMPLETION_INIT(&request->callback);
#endif

        unsigned retry = 0;
        do {
            // Realize the type of operation from data
            switch (request->op_type) {
            // Select appropriate action
            case HE_QAT_OP_MODEXP:
#ifdef HE_QAT_DEBUG
                printf("Offload request using instance #%d\n", next_instance);
#endif
#ifdef HE_QAT_PERF
                gettimeofday(&request->start, NULL);
#endif
                status = cpaCyLnModExp(
                    config->inst_config[next_instance].inst_handle,
                    (CpaCyGenFlatBufCbFunc)
                        request->callback_func,  // lnModExpCallback,
                    (void*)request, (CpaCyLnModExpOpData*)request->op_data,
                    &request->op_result);
                retry++;
                break;
            case HE_QAT_OP_NONE:
            default:
#ifdef HE_QAT_DEBUG
                printf("HE_QAT_OP_NONE to instance #%d\n", next_instance);
#endif
                retry = HE_QAT_MAX_RETRY;
                break;
            }

	    if (CPA_STATUS_RETRY == status) {
	        printf("CPA requested RETRY\n");
	        printf("RETRY count = %u\n",retry);
		pthread_exit(NULL); // halt the whole system
	    }

        } while (CPA_STATUS_RETRY == status && retry < HE_QAT_MAX_RETRY);

        // Ensure every call to perform operation is blocking for each endpoint
        if (CPA_STATUS_SUCCESS == status) {
	    // Global tracking of number of requests 
	    request_count += 1;
	    request_count_per_instance[next_instance] += 1;
//            printf("Instance %d Count %u\n",next_instance,request_count_per_instance[next_instance]);
            next_instance = (next_instance + 1) % instance_count;
//		printf("retry_count = %d\n",retry_count);
//            printf("SUCCESS Next Instance = %d\n",next_instance);
	// Wake up any blocked call to stop_perform_op, signaling that now it is
        // safe to terminate running instances. Check if this detereorate
        // performance.
        pthread_cond_signal(&config->inst_config[next_instance].ready);  // Prone to the lost wake-up problem
#ifdef HE_QAT_SYNC_MODE
            // Wait until the callback function has been called
            if (!COMPLETION_WAIT(&request->callback, TIMEOUT_MS)) {
                request->op_status = CPA_STATUS_FAIL;
                request->request_status = HE_QAT_STATUS_FAIL;  // Review it
                printf("Failed in COMPLETION WAIT\n");
            }

            // Destroy synchronization object
            COMPLETION_DESTROY(&request->callback);
#endif
        } else {
            request->op_status = CPA_STATUS_FAIL;
            request->request_status = HE_QAT_STATUS_FAIL;  // Review it
	    printf("Request Submission FAILED\n");
        }

#ifdef HE_QAT_DEBUG
        printf("Offloading completed by instance #%d\n", next_instance-1);
#endif

	// Reset pointer
	outstanding_requests.request[i] = NULL;
	request = NULL;
	
        }// for loop over batch of requests
        outstanding_requests.count = 0;
    }
    pthread_exit(NULL);
}

/// @brief
/// @function perform_op
/// Offload operation to QAT endpoints; for example, large number modular
/// exponentiation.
/// @param[in] HE_QAT_InstConfig *: contains the handle to CPA instance, pointer
/// the global buffer of requests.
void* start_perform_op(void* _inst_config) {
//    static unsigned int request_count = 0;
    if (NULL == _inst_config) {
        printf("Failed in start_perform_op: _inst_config is NULL.\n");
        pthread_exit(NULL);
    }

    HE_QAT_InstConfig* config = (HE_QAT_InstConfig*)_inst_config;

    CpaStatus status = CPA_STATUS_FAIL;

    // Start from zero or restart after stop_perform_op
    pthread_mutex_lock(&config->mutex);
    while (config->active) pthread_cond_wait(&config->ready, &config->mutex);

    // assert(0 == config->active);
    // assert(NULL == config->inst_handle);

    status = cpaCyStartInstance(config->inst_handle);
    config->status = status;
    if (CPA_STATUS_SUCCESS == status) {
        printf("Cpa CyInstance has successfully started.\n");
        status =
            cpaCySetAddressTranslation(config->inst_handle, sampleVirtToPhys);
    }

    pthread_cond_signal(&config->ready);
    pthread_mutex_unlock(&config->mutex);

    if (CPA_STATUS_SUCCESS != status) pthread_exit(NULL);

    // Start QAT instance and start polling
    pthread_t polling_thread;
    if (pthread_create(&polling_thread, config->attr, start_inst_polling,
                       (void*)config) != 0) {
        printf("Failed at creating and starting polling thread.\n");
        pthread_exit(NULL);
    }

    if (pthread_detach(polling_thread) != 0) {
        printf("Failed at detaching polling thread.\n");
        pthread_exit(NULL);
    }
    
    /* NEW CODE */
    HE_QAT_TaskRequestList outstanding_requests;
    for (unsigned int i = 0; i < HE_QAT_BUFFER_SIZE; i++) {
        outstanding_requests.request[i] = NULL;
    }
    outstanding_requests.count = 0;
    /* END NEW CODE */

    config->running = 1;
    config->active = 1;
    while (config->running) {
#ifdef HE_QAT_DEBUG
        printf("Try reading request from buffer. Inst #%d\n", config->inst_id);
#endif
	/* NEW CODE */
	unsigned long pending = request_count - response_count;
	unsigned long available = max_pending - ((pending < max_pending)?pending:max_pending);
#ifdef HE_QAT_DEBUG
	printf("[CHECK] request_count: %lu response_count: %lu pending: %lu available: %lu\n",
			request_count,response_count,pending,available);
#endif
	while (available < restart_threshold) {
#ifdef HE_QAT_DEBUG
	   printf("[WAIT]\n");
#endif
	   // argument passed in microseconds 
	   OS_SLEEP(650);
           pending = request_count - response_count;
	   available = max_pending - ((pending < max_pending)?pending:max_pending);
	}
#ifdef HE_QAT_DEBUG
	printf("[SUBMIT] request_count: %lu response_count: %lu pending: %lu available: %lu\n",
			request_count,response_count,pending,available);
#endif
	unsigned int max_requests = available;
	// Try consume maximum amount of data from butter to perform requested operation
        read_request_list(&outstanding_requests, &he_qat_buffer, max_requests);
	/* END NEW CODE  */

//	// Try consume data from butter to perform requested operation
//        HE_QAT_TaskRequest* request =
//            (HE_QAT_TaskRequest*)read_request(&he_qat_buffer);
//
//        if (NULL == request) {
//            pthread_cond_signal(&config->ready);
//            continue;
//        }
#ifdef HE_QAT_DEBUG
        printf("Offloading %u requests to the accelerator.\n", outstanding_requests.count);
#endif
        for (unsigned int i = 0; i < outstanding_requests.count; i++) {
	   HE_QAT_TaskRequest* request = outstanding_requests.request[i];
#ifdef HE_QAT_SYNC_MODE
        COMPLETION_INIT(&request->callback);
#endif
        unsigned retry = 0;
        do {
            // Realize the type of operation from data
            switch (request->op_type) {
            // Select appropriate action
            case HE_QAT_OP_MODEXP:
		//if (retry > 0) printf("Try offloading again last request\n");
#ifdef HE_QAT_DEBUG
                printf("Offload request using instance #%d\n", config->inst_id);
#endif
#ifdef HE_QAT_PERF
                gettimeofday(&request->start, NULL);
#endif
                status = cpaCyLnModExp(
                    config->inst_handle,
                    (CpaCyGenFlatBufCbFunc)
                        request->callback_func,  // lnModExpCallback,
                    (void*)request, (CpaCyLnModExpOpData*)request->op_data,
                    &request->op_result);
                retry++;
                break;
            case HE_QAT_OP_NONE:
            default:
#ifdef HE_QAT_DEBUG
                printf("HE_QAT_OP_NONE to instance #%d\n", config->inst_id);
#endif
                retry = HE_QAT_MAX_RETRY;
                break;
            }

	    if (CPA_STATUS_RETRY == status) {
	        printf("CPA requested RETRY\n");
	        printf("RETRY count: %u\n",retry);
                OS_SLEEP(600);
	    }

        } while (CPA_STATUS_RETRY == status && retry < HE_QAT_MAX_RETRY);

        // Ensure every call to perform operation is blocking for each endpoint
        if (CPA_STATUS_SUCCESS == status) {
	    // Global tracking of number of requests 
	    request_count += 1;
	    //printf("retry_count = %d\n",retry_count);
#ifdef HE_QAT_SYNC_MODE
            // Wait until the callback function has been called
            if (!COMPLETION_WAIT(&request->callback, TIMEOUT_MS)) {
                request->op_status = CPA_STATUS_FAIL;
                request->request_status = HE_QAT_STATUS_FAIL;  // Review it
                printf("Failed in COMPLETION WAIT\n");
            }

            // Destroy synchronization object
            COMPLETION_DESTROY(&request->callback);
#endif
        } else {
            request->op_status = CPA_STATUS_FAIL;
            request->request_status = HE_QAT_STATUS_FAIL;  // Review it
        }

	// Reset pointer
	outstanding_requests.request[i] = NULL;
	request = NULL;

        }// for loop over batch of requests
        outstanding_requests.count = 0;
       	
	// Wake up any blocked call to stop_perform_op, signaling that now it is
        // safe to terminate running instances. Check if this detereorate
        // performance.
        pthread_cond_signal(
            &config->ready);  // Prone to the lost wake-up problem
#ifdef HE_QAT_DEBUG
        printf("Offloading completed by instance #%d\n", config->inst_id);
#endif
    }
    pthread_exit(NULL);
}

/// @brief
/// @function
/// Stop first 'num_inst' number of cpaCyInstance(s), including their polling
/// and running threads.
/// @param[in] HE_QAT_InstConfig config Vector of created instances with their
/// configuration setup.
/// @param[in] num_inst Unsigned integer number indicating first number of
/// instances to be terminated.
void stop_perform_op(HE_QAT_InstConfig* config, unsigned num_inst) {
    // if () {
    // Stop runnning and polling instances
    // Release QAT instances handles
    if (NULL == config) return;

    CpaStatus status = CPA_STATUS_FAIL;
    for (unsigned i = 0; i < num_inst; i++) {
        pthread_mutex_lock(&config[i].mutex);
#ifdef HE_QAT_DEBUG
        printf("Try teardown HE QAT instance #%d.\n", i);
#endif
        while (0 == config[i].active) {
            pthread_cond_wait(&config[i].ready, &config[i].mutex);
        }
        if (CPA_STATUS_SUCCESS == config[i].status && config[i].active) {
#ifdef HE_QAT_DEBUG
            printf("Stop polling and running threads #%d\n", i);
#endif
            config[i].polling = 0;
            config[i].running = 0;
            OS_SLEEP(10);
#ifdef HE_QAT_DEBUG
            printf("Stop cpaCyInstance #%d\n", i);
#endif
            if (config[i].inst_handle == NULL) continue;
#ifdef HE_QAT_DEBUG
            printf("cpaCyStopInstance\n");
#endif
            status = cpaCyStopInstance(config[i].inst_handle);
            if (CPA_STATUS_SUCCESS != status) {
                printf("Failed to stop QAT instance #%d\n", i);
            }
        }
        pthread_cond_signal(&config[i].ready);
        pthread_mutex_unlock(&config[i].mutex);
    }
    //}

    return;
}

void stop_instances(HE_QAT_Config* _config) {
    if (NULL == _config) return;
    if (_config->active) _config->active = 0;
    if (_config->running) _config->running = 0;
    stop_perform_op(_config->inst_config, _config->count);
    return ;
}

