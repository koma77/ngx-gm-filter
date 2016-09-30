/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#include "ngx_http_gm_filter_module.h"
#include "ngx_http_gm_filter_convert.h"
#include "ngx_http_gm_filter_composite.h"


static ngx_int_t ngx_http_gm_image_send(ngx_http_request_t *r,
    ngx_http_gm_ctx_t *ctx, ngx_chain_t *in);

static ngx_uint_t ngx_http_gm_image_test(ngx_http_request_t *r,
    ngx_chain_t *in);

static ngx_int_t ngx_http_gm_image_read(ngx_http_request_t *r,
    ngx_chain_t *in);

static ngx_buf_t *ngx_http_gm_image_process(ngx_http_request_t *r);

static void ngx_http_gm_image_cleanup(void *data);
static void ngx_http_gm_image_length(ngx_http_request_t *r,
    ngx_buf_t *b);

static ngx_buf_t * ngx_http_gm_image_run_commands(ngx_http_request_t *r,
    ngx_http_gm_ctx_t *ctx);

static void *ngx_http_gm_create_conf(ngx_conf_t *cf);
static char *ngx_http_gm_merge_conf(ngx_conf_t *cf, void *parent,
    void *child);

static char *ngx_http_gm_gm(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_int_t ngx_http_gm_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_gm_init_worker(ngx_cycle_t *cycle);
static void ngx_http_gm_exit_worker(ngx_cycle_t *cycle);


static ngx_command_t  ngx_http_gm_commands[] = {

    { ngx_string("gm"),
      NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_gm_gm,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("gm_image_quality"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_gm_conf_t, image_quality),
      NULL },


    { ngx_string("gm_buffer"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_gm_conf_t, buffer_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_gm_module_ctx = {
    NULL,                        /* preconfiguration */
    ngx_http_gm_init,            /* postconfiguration */

    NULL,                        /* create main configuration */
    NULL,                        /* init main configuration */

    NULL,                        /* create server configuration */
    NULL,                        /* merge server configuration */

    ngx_http_gm_create_conf,     /* create location configuration */
    ngx_http_gm_merge_conf       /* merge location configuration */
};


ngx_module_t  ngx_http_gm_module = {
    NGX_MODULE_V1,
    &ngx_http_gm_module_ctx,        /* module context */
    ngx_http_gm_commands,           /* module directives */
    NGX_HTTP_MODULE,                /* module type */
    NULL,                           /* init master */
    NULL,                           /* init module */
    ngx_http_gm_init_worker,        /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    ngx_http_gm_exit_worker,        /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static ngx_str_t  ngx_http_gm_image_types[] = {
    ngx_string("image/jpeg"),
    ngx_string("image/gif"),
    ngx_string("image/png"),
    ngx_string("image/webp")
};

static ngx_int_t
ngx_http_gm_header_filter(ngx_http_request_t *r)
{
    off_t                          len;
    ngx_http_gm_ctx_t   *ctx;
    ngx_http_gm_conf_t  *conf;

    if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
        return ngx_http_next_header_filter(r);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_gm_module);

    if (ctx) {
        ngx_http_set_ctx(r, NULL, ngx_http_gm_module);
        return ngx_http_next_header_filter(r);
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_gm_module);

    if (conf->cmds == NULL) {
        return ngx_http_next_header_filter(r);
    }

    if (r->headers_out.content_type.len
            >= sizeof("multipart/x-mixed-replace") - 1
        && ngx_strncasecmp(r->headers_out.content_type.data,
                           (u_char *) "multipart/x-mixed-replace",
                           sizeof("multipart/x-mixed-replace") - 1)
           == 0)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "gm filter: multipart/x-mixed-replace response");

        return NGX_ERROR;
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_gm_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_gm_module);

    len = r->headers_out.content_length_n;

    if (len != -1 && len > (off_t) conf->buffer_size) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "gm filter: too big response: %O", len);

        return NGX_HTTP_UNSUPPORTED_MEDIA_TYPE;
    }

    if (len == -1) {
        ctx->length = conf->buffer_size;

    } else {
        ctx->length = (size_t) len;
    }

    if (r->headers_out.refresh) {
        r->headers_out.refresh->hash = 0;
    }

    r->main_filter_need_in_memory = 1;
    r->allow_ranges = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_gm_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_int_t                      rc;
    ngx_str_t                     *ct;
    ngx_chain_t                    out;
    ngx_http_gm_ctx_t   *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "image filter");

    if (in == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_gm_module);

    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    switch (ctx->phase) {

    case NGX_HTTP_GM_START:

        ctx->type = ngx_http_gm_image_test(r, in);


        if (ctx->type == NGX_HTTP_GM_IMAGE_NONE) {
            return ngx_http_filter_finalize_request(r,
                                              &ngx_http_gm_module,
                                              NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
        }

        /* override content type 

        ct = &ngx_http_gm_image_types[ctx->type - 1];
        r->headers_out.content_type_len = ct->len;
        r->headers_out.content_type = *ct;
        r->headers_out.content_type_lowcase = NULL;

        fall through */
        ctx->phase = NGX_HTTP_GM_IMAGE_READ;

    case NGX_HTTP_GM_IMAGE_READ:

        rc = ngx_http_gm_image_read(r, in);

        if (rc == NGX_AGAIN) {
            return NGX_OK;
        }

        if (rc == NGX_ERROR) {
            return ngx_http_filter_finalize_request(r,
                                              &ngx_http_gm_module,
                                              NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
        }

        /* fall through */

    case NGX_HTTP_GM_IMAGE_PROCESS:

        out.buf = ngx_http_gm_image_process(r);

        if (out.buf == NULL) {
            return ngx_http_filter_finalize_request(r,
                                              &ngx_http_gm_module,
                                              NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
        }

        out.next = NULL;

        /* by koma */
        ctx->type = ngx_http_gm_image_test(r, &out);

        if (ctx->type == NGX_HTTP_GM_IMAGE_NONE) {
            return ngx_http_filter_finalize_request(r,
                                              &ngx_http_gm_module,
                                              NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
        }

        /* override content type */

        ct = &ngx_http_gm_image_types[ctx->type - 1];
        r->headers_out.content_type_len = ct->len;
        r->headers_out.content_type = *ct;
        r->headers_out.content_type_lowcase = NULL;

        ctx->phase = NGX_HTTP_GM_IMAGE_PASS;

        return ngx_http_gm_image_send(r, ctx, &out);

    case NGX_HTTP_GM_IMAGE_PASS:

        return ngx_http_next_body_filter(r, in);

    default: /* NGX_HTTP_GM_IMAGE_DONE */

        rc = ngx_http_next_body_filter(r, NULL);

        /* NGX_ERROR resets any pending data */
        return (rc == NGX_OK) ? NGX_ERROR : rc;
    }
}


static ngx_int_t
ngx_http_gm_image_send(ngx_http_request_t *r, ngx_http_gm_ctx_t *ctx,
    ngx_chain_t *in)
{
    ngx_int_t  rc;

    rc = ngx_http_next_header_filter(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return NGX_ERROR;
    }

    rc = ngx_http_next_body_filter(r, in);

    if (ctx->phase == NGX_HTTP_GM_IMAGE_DONE) {
        /* NGX_ERROR resets any pending data */
        return (rc == NGX_OK) ? NGX_ERROR : rc;
    }

    return rc;
}


static ngx_uint_t
ngx_http_gm_image_test(ngx_http_request_t *r, ngx_chain_t *in)
{
    u_char  *p;

    p = in->buf->pos;

    if (in->buf->last - p < 16) {
        return NGX_HTTP_GM_IMAGE_NONE;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "image filter: \"%c%c\"", p[0], p[1]);

    if (p[0] == 0xff && p[1] == 0xd8) {

        /* JPEG */

        return NGX_HTTP_GM_IMAGE_JPEG;

    } else if (p[0] == 'G' && p[1] == 'I' && p[2] == 'F' && p[3] == '8'
               && p[5] == 'a')
    {
        if (p[4] == '9' || p[4] == '7') {
            /* GIF */
            return NGX_HTTP_GM_IMAGE_GIF;
        }

    } else if (p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G'
               && p[4] == 0x0d && p[5] == 0x0a && p[6] == 0x1a && p[7] == 0x0a)
    {
        /* PNG */

        return NGX_HTTP_GM_IMAGE_PNG;
    } else if (p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F' && p[8] == 'W')
    {
        /* WEBP */

        return NGX_HTTP_GM_IMAGE_WEBP;
    }


    return NGX_HTTP_GM_IMAGE_NONE;
}


static ngx_int_t
ngx_http_gm_image_read(ngx_http_request_t *r, ngx_chain_t *in)
{
    u_char                       *p;
    size_t                        size, rest;
    ngx_buf_t                    *b;
    ngx_chain_t                  *cl;
    ngx_http_gm_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_gm_module);

    if (ctx->image_blob == NULL) {
        ctx->image_blob = ngx_palloc(r->pool, ctx->length);
        if (ctx->image_blob == NULL) {
            return NGX_ERROR;
        }

        ctx->last = ctx->image_blob;
    }

    p = ctx->last;

    for (cl = in; cl; cl = cl->next) {

        b = cl->buf;
        size = b->last - b->pos;

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "gm image buf: %uz", size);

        rest = ctx->image_blob + ctx->length - p;
        size = (rest < size) ? rest : size;

        p = ngx_cpymem(p, b->pos, size);
        b->pos += size;

        if (b->last_buf) {
            ctx->last = p;
            return NGX_OK;
        }
    }

    ctx->last = p;
    r->connection->buffered |= NGX_HTTP_IMAGE_BUFFERED;

    return NGX_AGAIN;
}


static ngx_buf_t *
ngx_http_gm_image_process(ngx_http_request_t *r)
{
    ngx_http_gm_ctx_t   *ctx;

    r->connection->buffered &= ~NGX_HTTP_IMAGE_BUFFERED;

    ctx = ngx_http_get_module_ctx(r, ngx_http_gm_module);

    return ngx_http_gm_image_run_commands(r, ctx);
}


static ngx_buf_t *
ngx_http_gm_image_run_commands(ngx_http_request_t *r, ngx_http_gm_ctx_t *ctx)
{
    ngx_buf_t           *b;
    ngx_int_t            rc;
    ngx_http_gm_conf_t  *gmcf;

    u_char         *image_blob;

    ImageInfo      *image_info;
    Image          *image;
    ExceptionInfo   exception;

    ngx_uint_t      i;
    ngx_http_gm_command_t *gm_cmd;
    ngx_http_gm_command_t *gm_cmds;
    u_char         *out_blob;
    ngx_uint_t      out_len;

    ngx_pool_cleanup_t            *cln;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "gm: entering gm image run commands");

    gmcf = ngx_http_get_module_loc_conf(r, ngx_http_gm_module);
    if (gmcf->cmds == NULL || gmcf->cmds->nelts == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "gm filter: run command failed, reason: no command");
        return NULL;
    }

    GetExceptionInfo(&exception);

    image_blob = ctx->image_blob;

    image_info = CloneImageInfo((ImageInfo *) NULL);

    /* blob to image */
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "gm: blob to image");

    image = BlobToImage(image_info, image_blob, ctx->length, &exception);
    if (image == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "gm filter: blob to image failed, "
                      "severity: %O reason: %s, description: %s",
                      exception.severity, exception.reason,
                      exception.description);

        goto failed1;
    }


    /* run commands */
    rc = NGX_OK;
    gm_cmds = gmcf->cmds->elts;
    for (i = 0; i < gmcf->cmds->nelts; ++i) {
        gm_cmd = &gm_cmds[i];
        if (gm_cmd->type == NGX_HTTP_GM_COMPOSITE_CMD) {
            rc = composite_image(r, &gm_cmd->composite_options, &image);
        } else if (gm_cmd->type == NGX_HTTP_GM_CONVERT_CMD) {
            rc = convert_image(r, &gm_cmd->convert_options, &image);
        }

        if (rc != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                         "gm filter: run command failed, comamnd: \"%s\"",
                         gm_cmd->cmd);

            goto failed2;
        }
    }

    /* image to blob */
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "gm: image to blob");

    image_info->quality = gmcf->image_quality;

    out_blob = ImageToBlob(image_info, image,  &out_len, &exception);
    if (out_blob == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "gm filter: image to blob failed, "
                      "severity: %O reason: %s, description: %s",
                      exception.severity, exception.reason,
                      exception.description);
        goto failed2;
    }

    /* image out to buf */
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "gm: blob to buf");

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                     "gm filter: alloc buf_t failed");
        goto failed3;
    }

    b->pos = out_blob;
    b->last = out_blob + out_len;
    b->memory = 1;
    b->last_buf = 1;

    ngx_http_gm_image_length(r, b);

    /* register cleanup */
    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "gm filter: register cleanup failed");
        goto failed3;
    }

    cln->handler = ngx_http_gm_image_cleanup;
    cln->data = out_blob;


    /* destory imput blob */
    ngx_pfree(r->pool, ctx->image_blob);

    /* destory iamge */
    DestroyImage(image);
    /* destory image info */
    DestroyImageInfo(image_info);
    DestroyExceptionInfo(&exception);

    return b;

failed3:
    /* clean out blob */
    MagickFree(out_blob);

failed2:
    /* destory iamge */
    DestroyImage(image);

failed1:
    /* destory image info */
    DestroyImageInfo(image_info);
    DestroyExceptionInfo(&exception);

    return NULL;
}


static void
ngx_http_gm_image_cleanup(void *out_blob)
{
    dd("cleanup iamge out_blob");
    MagickFree(out_blob);
}


static void
ngx_http_gm_image_length(ngx_http_request_t *r, ngx_buf_t *b)
{
    r->headers_out.content_length_n = b->last - b->pos;

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
    }

    r->headers_out.content_length = NULL;
}


static void *
ngx_http_gm_create_conf(ngx_conf_t *cf)
{
    ngx_http_gm_conf_t  *gmcf;

    gmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_gm_conf_t));
    if (gmcf == NULL) {
        return NULL;
    }

    gmcf->buffer_size = NGX_CONF_UNSET_SIZE;
    gmcf->image_quality = NGX_CONF_UNSET_SIZE;

    return gmcf;
}


static char *
ngx_http_gm_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_gm_conf_t *prev = parent;
    ngx_http_gm_conf_t *conf = child;

    if (conf->cmds == NULL && prev->cmds != NULL) {
        conf->cmds = prev->cmds;
    }

    ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size,
                              4 * 1024 * 1024);

    ngx_conf_merge_size_value(conf->image_quality, prev->image_quality,
                              75);
    return NGX_CONF_OK;
}


static char *
ngx_http_gm_gm(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_gm_conf_t                *gmcf = conf;

    ngx_str_t                         *value;

    ngx_http_gm_command_t             *gm_cmd;

    ngx_int_t                          rc;
    ngx_uint_t                         i;

    ngx_array_t                       *args;

    dd("entering");

    args  = cf->args;
    value = cf->args->elts;

    i = 1;

    if (args->nelts < 2) {
        return NGX_CONF_ERROR;
    }

    if (gmcf->cmds == NULL) {
        gmcf->cmds = ngx_array_create(cf->pool, 1, sizeof(ngx_http_gm_command_t));
        if (gmcf->cmds == NULL) {
            goto failed;
        }
    }

    gm_cmd = ngx_array_push(gmcf->cmds);
    if (gm_cmd == NULL) {
        goto alloc_failed;
    }

    if (ngx_strcmp(value[i].data, "convert") == 0) {

        gm_cmd->type = NGX_HTTP_GM_CONVERT_CMD;
        gm_cmd->cmd = "convert";
        rc = parse_convert_options(cf, args, i, &gm_cmd->convert_options);
        if (rc != NGX_OK) {
            goto failed;
        }

    } else if (ngx_strcmp(value[i].data, "composite") == 0) {

        gm_cmd->type = NGX_HTTP_GM_COMPOSITE_CMD;
        gm_cmd->cmd = "composite";
        rc = parse_composite_options(cf, args, i, &gm_cmd->composite_options);
        if (rc != NGX_OK) {
            goto failed;
        }

    } else {

        goto failed;
    }

    dd("parse config okay");

    return NGX_CONF_OK;

alloc_failed:
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "alloc failed \"%V\"",
                       &value[i]);

    return NGX_CONF_ERROR;

failed:

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid parameter for command, \"%V\"",
                       &value[i]);

    return NGX_CONF_ERROR;
}


static ngx_int_t
ngx_http_gm_init_worker(ngx_cycle_t *cycle)
{
    InitializeMagick("logs");

    return NGX_OK;
}


static void
ngx_http_gm_exit_worker(ngx_cycle_t *cycle)
{
    DestroyMagick();
}


static ngx_int_t
ngx_http_gm_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_gm_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_gm_body_filter;


    return NGX_OK;
}

