/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#include <mod_ivs.h>

/*
typedef void (async_job_proc_t)(void *data);

typedef struct {
    uint32_t                jid;
    switch_memory_pool_t    *pool;
    ivs_session_t           *ivs_session;
    async_job_proc_t        *proc;
    void                    *udata;
} ivs_asyncj_conf_t;

static void *SWITCH_THREAD_FUNC ivs_async_job_thread(switch_thread_t *thread, void *obj) {
    volatile ivs_asyncj_conf_t *_ref = (ivs_asyncj_conf_t *) obj;
    ivs_asyncj_conf_t *params = (ivs_asyncj_conf_t *) _ref;
    switch_memory_pool_t *pool_local = params->pool;

    if(ivs_session_take(params->ivs_session)) {
        params->proc(params->udata);
        ivs_session_release(params->ivs_session);
    }

    if(pool_local) {
        switch_core_destroy_memory_pool(&pool_local);
    }

    thread_finished();
    return NULL;
}


uint32_t ivs_async_job_start(ivs_session_t *ivs_session, void *data) {
    uint32_t jid = ivs_gen_job_id(ivs_session);

    //
    //
    //

    return jid;
}

*/
