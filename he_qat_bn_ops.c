
#include "cpa.h"
#include "cpa_cy_ln.h"

#include "cpa_sample_utils.h"

#include "he_qat_types.h"
#include "he_qat_bn_ops.h"

#include <pthread.h>
#include <assert.h>

/// @brief 
/// @function 
/// Callback function for lnModExpPerformOp. It performs any data processing 
/// required after the modular exponentiation.
static void lnModExpCallback(void *pCallbackTag, // This type can be variable
                          CpaStatus status,
                          void *pOpData,         // This is fixed -- please swap it
                          CpaFlatBuffer *pOut)
{
    HE_QAT_TaskRequest *request = NULL;

    //if (CPA_STATUS_SUCCESS != status) {
    //    // Update request status as an error (in pOpData)
    //    
    //    return ;
    //}

    // Check if input data for the op is available and do something
    if (NULL != pCallbackTag) {
        // Read request data
        request = (HE_QAT_TaskRequest *) pCallbackTag;

        // Collect the device output in pOut
		
        request->op_status = status;
	if (pOpData == &request->op_data)
	    // Mark request as complete or ready to be used
            request->request_status = HE_QAT_READY;
	else 
            request->request_status = HE_QAT_FAIL;
        
	COMPLETE((struct COMPLETION_STRUCT *)request->callback);
    }

    // Asynchronous call needs to send wake-up signal to semaphore  
   // if (NULL != pCallbackTag) {
   //     COMPLETE((struct COMPLETION_STRUCT *)pCallbackTag);
   // }

    return ;
}


/// @brief
/// @function
/// Thread-safe producer implementation for the shared request buffer.
/// Stores requests in a buffer that will be offload to QAT devices.
void submit_request(HE_QAT_RequestBuffer *_buffer, void *args)
{
    pthread_mutex_lock(&_buffer->mutex);
   
    while (_bufffer->count >= BSIZE)
        pthread_cond_wait(&_buffer->any_free_slot, &b->mutex);

    assert(_buffer->count < BSIZE);

    _buffer->data[b->next_free_slot++] = args;

    _buffer->next_free_slot %= HE_QAT_BUFFER_SIZE;
    _buffer->count++;

    /* now: either b->occupied < BSIZE and b->nextin is the index
       of the next empty slot in the buffer, or
       b->occupied == BSIZE and b->nextin is the index of the
       next (occupied) slot that will be emptied by a consumer
       (such as b->nextin == b->nextout) */

    pthread_cond_signal(&_buffer->any_more_data);
    pthread_mutex_unlock(&_buffer->mutex);
}

/// @brief
/// @function
/// Thread-safe consumer implementation for the shared request buffer.
/// Read requests from a buffer to finally offload the work to QAT devices.
HE_QAT_TaskRequest *read_request(HE_QAT_RequestBuffer *_buffer)
{
    void *item = NULL;
    pthread_mutex_lock(&_buffer->mutex);
    while(_buffer->count <= 0)
        pthread_cond_wait(&_buffer->any_more_data, &_buffer->mutex);

    assert(_buffer->count > 0);

    //printf("[%02d]:",_buffer->next_data_slot);
    item = _buffer->data[b->next_data_slot++];
    _buffer->next_data_slot %= HE_QAT_BUFFER_SIZE;
    _buffer->count--;

    /* now: either b->occupied > 0 and b->nextout is the index
       of the next occupied slot in the buffer, or
       b->occupied == 0 and b->nextout is the index of the next
       (empty) slot that will be filled by a producer (such as
       b->nextout == b->nextin) */

    pthread_cond_signal(&_buffer->any_free_slot);
    pthread_mutex_unlock(&_buffer->mutex);

    return (HE_QAT_TaskRequest *) (item);
}

/// @brief 
/// @function perform_op
/// Offload operation to QAT endpoints; for example, large number modular exponentiation.
/// @param[in] HE_QAT_InstConfig *: contains the handle to CPA instance, pointer the global buffer of requests.
void *perform_op(void *_inst_config)
{
    CpaStatus status = CPA_STATUS_FAIL;
    HE_QAT_InstConfig *config = (HE_QAT_InstConfig *) _inst_config;
    config->running = 1;
    while (config->running) {
	// Try consume data from butter to perform requested operation
	HE_QAT_TaskRequest *request = 
	            (HE_QAT_TaskRequest *) read_request(config->he_qat_buffer);

        if (!request) continue;

        unsigned retry = 0;
        do {
	    // Realize the type of operation from data
            switch (request->op_type) { 
	        // Select appropriate action
                case HE_QAT_MODEXP:
                    status = cpaCyLnModExp(config->inst_handle,
        			    lnModExpCallback,
        			    (void *) request->callback,
        			    &request->op_data,
        			    &request->op_output);
		    retry++;
		    break;
		case HE_QAT_NO_OP:
		default:
		    retry = HE_QAT_MAX_RETRY; 
		    break;
	    }
            
        } while (CPA_STATUS_RETRY == status && retry < HE_QAT_MAX_RETRY);

	// Update the status of the request
	request->op_status = status;
	if (CPA_STATUS_SUCCESS != status) 
            request->request_status = HE_QAT_FAIL;
	else 
            request->request_status = HE_QAT_READY;
    }
    pthread_exit(NULL);
    //return NULL;
}


CpaStatus bnModExpPerformOp(BIGNUM *r, BIGNUM *b, BIGNUM *e, BIGNUM *m, int nbits)
{
        
}

