#include <nginx.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
	ngx_http_upstream_conf_t upstream;
	// Add more here
	ngx_uint_t db;
	ngx_str_t key;
} ngx_http_redis_loc_conf_t;

static ngx_conf_bitmask_t  ngx_http_redis_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("not_found"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};

static ngx_int_t ngx_http_redis_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_redis_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_redis_process_header(ngx_http_request_t *r);
static void
ngx_http_redis_finalize_request(ngx_http_request_t *r, ngx_int_t rc);

static ngx_int_t ngx_http_redis_input_filter(void *data, ssize_t bytes);

static void *ngx_http_redis_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_redis_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static char* 
ngx_http_redis_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_command_t ngx_http_redis_commands[] = {
	{
		ngx_string("redis_pass"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_http_redis_pass,
		NGX_HTTP_LOC_CONF_OFFSET,
		0,
		NULL
	},
	{
		ngx_string("redis_db"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_redis_loc_conf_t, db),
		NULL
	},
	{	
		ngx_string("redis_key"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_redis_loc_conf_t, key),
		NULL
	},
    {
		ngx_string("redis_connect_timeout"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_msec_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_redis_loc_conf_t, upstream.connect_timeout),
		NULL
	},
	{
		ngx_string("redis_send_timeout"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_msec_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_redis_loc_conf_t, upstream.send_timeout),
		NULL
	},
	{
		ngx_string("redis_read_timeout"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_msec_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_redis_loc_conf_t, upstream.read_timeout),
		NULL
	},
	{
		ngx_string("redis_next_upstream"),
		NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
		ngx_conf_set_bitmask_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_redis_loc_conf_t, upstream.next_upstream),
		&ngx_http_redis_next_upstream_masks
	},
	ngx_null_command,
};

static ngx_http_module_t ngx_http_redis_module_ctx = {
	NULL,	/* preconfiguration */
	NULL,	/* postconfiguration */

	NULL,	/* create main configuration */
	NULL,	/* init main configuration */

	NULL,	/* create service configuation */
	NULL,	/* merge service configuration */

	ngx_http_redis_create_loc_conf,	/* create location configuration */
	ngx_http_redis_merge_loc_conf	/* merge location configuration */
};


ngx_module_t ngx_http_redis_module = {
	NGX_MODULE_V1,
	&ngx_http_redis_module_ctx,		/* module context */
	ngx_http_redis_commands,		/* module directives */
	NGX_HTTP_MODULE,				/* module type */
	NULL,							/* init master */
	NULL,							/* init module */
	NULL,							/* init process */
	NULL,							/* init thread */
	NULL,							/* exit thread */
	NULL,							/* exit process */
	NULL,							/* exit master */
	NGX_MODULE_V1_PADDING
};


/* config */
static void *
ngx_http_redis_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_redis_loc_conf_t  *conf;
	
	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_redis_loc_conf_t));
	if (conf == NULL) {
	    return NULL;
	}

	conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

	conf->upstream.buffering = 0;
	/* Not used? */
	conf->upstream.pass_request_headers = 0;
	conf->upstream.pass_request_body = 0;

	conf->db = NGX_CONF_UNSET;
	conf->key.data = NULL;

	return conf;
}


static char *
ngx_http_redis_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
	ngx_http_redis_loc_conf_t *prev = (ngx_http_redis_loc_conf_t*) parent;
	ngx_http_redis_loc_conf_t *conf = (ngx_http_redis_loc_conf_t*) child;

	ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

	ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
		prev->upstream.next_upstream,
		(NGX_CONF_BITMASK_SET | NGX_HTTP_UPSTREAM_FT_ERROR | NGX_HTTP_UPSTREAM_FT_TIMEOUT));

	if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF)
	{
		conf->upstream.next_upstream = NGX_CONF_BITMASK_SET | NGX_HTTP_UPSTREAM_FT_OFF;
	}

	if (conf->upstream.upstream == NULL)
	{
		conf->upstream.upstream = prev->upstream.upstream;
	}

	ngx_conf_merge_uint_value(conf->db, prev->db, 0);
	ngx_conf_merge_str_value(conf->key, prev->key, "");

	return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_redis_handler(ngx_http_request_t *r)
{
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "ngx_http_redis_handler start...");

	ngx_http_redis_loc_conf_t *rlcf;
	ngx_http_upstream_t *u;
	ngx_int_t rc;

	/* set up upstream structure */
	if (ngx_http_upstream_create(r) != NGX_OK)
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_http_upstream_create() failed");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	u = r->upstream;
	ngx_str_set(&u->schema, "redis://");

	rlcf = ngx_http_get_module_loc_conf(r, ngx_http_redis_module);
	u->conf = &rlcf->upstream;
	
	/* attach the callback functions */
	u->create_request = ngx_http_redis_create_request;
	u->reinit_request = ngx_http_redis_reinit_request;
	u->process_header = ngx_http_redis_process_header;
	u->finalize_request = ngx_http_redis_finalize_request;

	u->input_filter = ngx_http_redis_input_filter;

	rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);
	if (rc > NGX_HTTP_SPECIAL_RESPONSE)
		return rc;
	return NGX_DONE;
}


/* callbacks */
static ngx_int_t ngx_http_redis_create_request(ngx_http_request_t *r)
{
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "ngx_http_redis_create_request start...");

	ngx_http_redis_loc_conf_t * rlcf;
	ngx_chain_t *cl, *body;
	ngx_buf_t *buf, *b;

	/* Do not forget to change the following offset when modifying the query string */
	ngx_str_t query = ngx_string("SELECT %ui\r\nRPUSH %V ");
	size_t len = query.len - 5;

	rlcf = ngx_http_get_module_loc_conf(r, ngx_http_redis_module);
	/* FIXME:  */
	len += (rlcf->db > 9 ? 2 : 1) + rlcf->key.len;

	/* Create temporary buffer for request with size len. */
    buf = ngx_create_temp_buf(r->pool, len);
    if (buf == NULL) {
        return NGX_ERROR;
    }
	ngx_snprintf(buf->pos, len, (char*)query.data, rlcf->db, &rlcf->key);
	buf->last = buf->pos + len;

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = buf;
    cl->next = NULL;

	body = r->upstream->request_bufs;
	r->upstream->request_bufs = cl;
	while (body)
	{
		b = ngx_alloc_buf(r->pool);
		if (b == NULL)
			return NGX_ERROR;

		ngx_memcpy(b, body->buf, sizeof(ngx_buf_t));
		cl->next = ngx_alloc_chain_link(r->pool);
		if (cl->next == NULL)
			return NGX_ERROR;

		cl = cl->next;
		cl->buf = b;
		body = body->next;
	}
	*cl->buf->last++ = CR; *cl->buf->last++ = LF;
	cl->next = NULL;

	for (cl = r->upstream->request_bufs; cl != NULL; cl = cl->next)
	{
		ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
			"redis request '%*s'", cl->buf->last - cl->buf->pos, cl->buf->pos);
	}

	return NGX_OK;
}


static ngx_int_t ngx_http_redis_reinit_request(ngx_http_request_t *r)
{
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "ngx_http_redis_reinit_request start...");
	return NGX_OK;
}


static u_char* advance(u_char *pos, u_char *last)
{
	while (pos != last && *pos != LF)
		pos++;
	return pos;
}


/**
 * We need to process two commands here, SELECT and RPUSH
 *
 * SELECT returns simple string, for example "+OK\r\n"
 * RPUSH returns interger, for example ":1000\r\n"
**/
static ngx_int_t ngx_http_redis_process_header(ngx_http_request_t *r)
{
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "ngx_http_redis_process_header start...");

	ngx_str_t line;
	u_char *p, lookahead;

	ngx_http_upstream_t *u;
	int i = 0, try_times = 0;

	u = r->upstream;
	p = u->buffer.pos;

	lookahead = *p;
	switch (lookahead)
	{
	case '+':
		try_times = 2;
	case '-':
		try_times = 1;
	default:
		goto INVALID;
	}
	
	for (i = 0; i < try_times; ++i)
	{
		p = advance(p, u->buffer.last);
		if (p == u->buffer.last) /* LF is not found */
			return NGX_AGAIN;
	}

	line.data = u->buffer.pos;
	line.len = p - u->buffer.pos - 1;

	if (lookahead == '-')
	{
		ngx_log_error(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
			"redis reply with error message: <%V>", &line);
		u->headers_in.status_n = 502;
		u->state->status = 502;
		return NGX_OK;
	}
	else
	{
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
			"redis: <%V>", &line);
		u->headers_in.status_n = 200;
		u->state->status = 200;

		/* Set position to the first symbol of data and return */
		u->buffer.pos = p + 1;
		return NGX_OK;
	}

INVALID:
	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		"redis reply with invalid response: <%.*s>", u->buffer.last - u->buffer.pos, u->buffer.pos);
    return NGX_HTTP_UPSTREAM_INVALID_HEADER;
}


static void
ngx_http_redis_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http redis request");
}


static ngx_int_t ngx_http_redis_input_filter(void *data, ssize_t bytes)
{
	ngx_http_request_t *r = data;

	ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
		"ngx_http_redis_input_filter() --> len %z", bytes);
	return NGX_OK;
}


static char* 
ngx_http_redis_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	ngx_http_redis_loc_conf_t *rlcf = conf;

	ngx_str_t *value;
	ngx_http_core_loc_conf_t *clcf;
	ngx_url_t url;

	if (rlcf->upstream.upstream) {
		return "is duplicate";
	}

	value = cf->args->elts;

    ngx_memzero(&url, sizeof(ngx_url_t));

    url.url = value[1];
    url.no_resolve = 1;

	ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
		"ngx_http_redis_pass --> url: %V", url.url);

    rlcf->upstream.upstream = ngx_http_upstream_add(cf, &url, 0);
    if (rlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

	clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
	clcf->handler = ngx_http_redis_handler;

	if (clcf->name.data[clcf->name.len - 1] == '/') {
		clcf->auto_redirect = 1;
	}

	return NGX_CONF_OK;
}