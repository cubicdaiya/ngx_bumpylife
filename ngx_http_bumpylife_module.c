#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static ngx_uint_t ngx_http_bumpylife_limit;
static ngx_uint_t ngx_http_bumpylife_count;
static ngx_uint_t ngx_http_bumpylife_exiting;
static ngx_pid_t *ngx_http_bumpylife_target_pid;
static ngx_str_t  ngx_http_bumpylife_shm = ngx_string("ngx_http_bumpylife_shm");

typedef struct ngx_http_bumpylife_conf_t {
    ngx_flag_t enable;
    ngx_uint_t min;
    ngx_uint_t max;
    ngx_shm_zone_t *shm_zone;
} ngx_http_bumpylife_conf_t;

static ngx_int_t ngx_http_bumpylife_init(ngx_conf_t *cf);
static void *ngx_http_bumpylife_create_conf(ngx_conf_t *cf);
static char *ngx_http_bumpylife_init_conf(ngx_conf_t *cf, void *conf);
static ngx_int_t ngx_http_bumpylife_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_bumpylife_shm_zone_init(ngx_shm_zone_t *shm_zone, void *data);

static ngx_command_t ngx_http_bumpylife_commands[] = {

    {
        ngx_string("bumpylife"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_bumpylife_conf_t, enable),
        NULL
    },

    {
        ngx_string("bumpylife_min"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_bumpylife_conf_t, min),
        NULL
    },

    {
        ngx_string("bumpylife_max"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_bumpylife_conf_t, max),
        NULL
    },

    ngx_null_command
};

static ngx_http_module_t ngx_http_bumpylife_module_ctx = {
    NULL,                              /* preconfiguration */
    ngx_http_bumpylife_init,           /* postconfiguration */

    ngx_http_bumpylife_create_conf,    /* create main configuration */
    ngx_http_bumpylife_init_conf,      /* init main configuration */

    NULL,                              /* create server configuration */
    NULL,                              /* merge server configuration */

    NULL,                              /* create location configuration */
    NULL                               /* merge location configuration */
};

ngx_module_t ngx_http_bumpylife_module = {
    NGX_MODULE_V1,
    &ngx_http_bumpylife_module_ctx, /* module context */
    ngx_http_bumpylife_commands,    /* module directives */
    NGX_HTTP_MODULE,                /* module type */
    NULL,                           /* init master */
    NULL,                           /* init module */
    NULL,                           /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    NULL,                           /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_http_bumpylife_init(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_handler_pt        *h;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_bumpylife_handler;

    ngx_http_bumpylife_limit = 0;
    ngx_http_bumpylife_count = 0;
    ngx_http_bumpylife_exiting = 0;

    return NGX_OK;
}

static void *ngx_http_bumpylife_create_conf(ngx_conf_t *cf)
{
    ngx_http_bumpylife_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_bumpylife_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable   = NGX_CONF_UNSET;
    conf->min      = NGX_CONF_UNSET_UINT;
    conf->max      = NGX_CONF_UNSET_UINT;
    conf->shm_zone = NGX_CONF_UNSET_PTR;

    return conf;
}

static char *ngx_http_bumpylife_init_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_bumpylife_conf_t *blcf = conf;
    ngx_shm_zone_t            *shm_zone;

    ngx_conf_init_value(blcf->enable, 0);
    ngx_conf_init_uint_value(blcf->min, 0);
    ngx_conf_init_uint_value(blcf->max, 0);
    ngx_conf_init_ptr_value(blcf->shm_zone, NULL);

    shm_zone = ngx_shared_memory_add(cf, &ngx_http_bumpylife_shm, 10240, &ngx_http_bumpylife_module);

    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    if (shm_zone->data) {
        return NGX_CONF_ERROR;
    }

    shm_zone->init = ngx_http_bumpylife_shm_zone_init;
    shm_zone->data = NULL;

    blcf->shm_zone = shm_zone;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_bumpylife_handler(ngx_http_request_t *r)
{
    ngx_http_bumpylife_conf_t *blcf;
    ngx_slab_pool_t           *shpool;

    blcf = ngx_http_get_module_main_conf(r, ngx_http_bumpylife_module);

    if (!blcf->enable) {
        return NGX_DECLINED;
    }

    if (blcf->min == 0 || blcf->max == 0) {
        return NGX_DECLINED;
    }

    if (blcf->min > blcf->max) {
        return NGX_DECLINED;
    }

    if (ngx_http_bumpylife_exiting) {
        return NGX_DECLINED;
    }

    if (ngx_http_bumpylife_limit == 0) {
        srand(ngx_time() ^ ngx_pid);
        ngx_http_bumpylife_limit = blcf->min + rand() % (blcf->max - blcf->min);
    }

    /*
    printf("pid: %zd, mutex:%zd\n", ngx_pid, *ngx_http_bumpylife_target_pid);
    printf("%zd, %zd, %zd, %zd\n", blcf->min, blcf->max, ngx_http_bumpylife_limit, ngx_http_bumpylife_count);
    */

    ngx_http_bumpylife_count++;

    shpool = (ngx_slab_pool_t *) blcf->shm_zone->shm.addr;

    ngx_shmtx_lock(&shpool->mutex);

    if (*ngx_http_bumpylife_target_pid != 0) {
        if (kill(*ngx_http_bumpylife_target_pid, SIGQUIT) == -1) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_errno,
                          "kill(%P, %d) failed", ngx_pid, SIGQUIT);
        }
        *ngx_http_bumpylife_target_pid = 0;
    }

    if (*ngx_http_bumpylife_target_pid == 0 && ngx_http_bumpylife_count > ngx_http_bumpylife_limit) {
        *ngx_http_bumpylife_target_pid = ngx_pid;

        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                      "the count of requests to be processed overed the limit: "
                      "pid -> %z, count -> %z, limit -> %z",
                      ngx_pid, ngx_http_bumpylife_count, ngx_http_bumpylife_limit);

        ngx_http_bumpylife_exiting = 1;
    }

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_DECLINED;
}

static ngx_int_t ngx_http_bumpylife_shm_zone_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_slab_pool_t            *shpool;

    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        ngx_http_bumpylife_target_pid = shpool->data;
        return NGX_OK;
    }

    ngx_http_bumpylife_target_pid = ngx_slab_alloc(shpool, sizeof(ngx_pid_t *));
    if (ngx_http_bumpylife_target_pid == NULL) {
        return NGX_ERROR;
    }
    *ngx_http_bumpylife_target_pid = 0;

    return NGX_OK;
}
