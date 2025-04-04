// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"


// ----------------------------------------------------------------------------
// /api/v2/contexts API

typedef enum __attribute__ ((__packed__)) {
    FTS_MATCHED_NONE = 0,
    FTS_MATCHED_HOST,
    FTS_MATCHED_CONTEXT,
    FTS_MATCHED_INSTANCE,
    FTS_MATCHED_DIMENSION,
    FTS_MATCHED_LABEL,
    FTS_MATCHED_ALERT,
    FTS_MATCHED_ALERT_INFO,
    FTS_MATCHED_FAMILY,
    FTS_MATCHED_TITLE,
    FTS_MATCHED_UNITS,
} FTS_MATCH;

static const char *fts_match_to_string(FTS_MATCH match) {
    switch(match) {
        case FTS_MATCHED_HOST:
            return "HOST";

        case FTS_MATCHED_CONTEXT:
            return "CONTEXT";

        case FTS_MATCHED_INSTANCE:
            return "INSTANCE";

        case FTS_MATCHED_DIMENSION:
            return "DIMENSION";

        case FTS_MATCHED_ALERT:
            return "ALERT";

        case FTS_MATCHED_ALERT_INFO:
            return "ALERT_INFO";

        case FTS_MATCHED_LABEL:
            return "LABEL";

        case FTS_MATCHED_FAMILY:
            return "FAMILY";

        case FTS_MATCHED_TITLE:
            return "TITLE";

        case FTS_MATCHED_UNITS:
            return "UNITS";

        default:
            return "NONE";
    }
}

struct rrdcontext_to_json_v2_entry {
    size_t count;
    STRING *id;
    STRING *family;
    uint32_t priority;
    time_t first_time_s;
    time_t last_time_s;
    RRD_FLAGS flags;
    FTS_MATCH match;
};

typedef struct full_text_search_index {
    size_t searches;
    size_t string_searches;
    size_t char_searches;
} FTS_INDEX;

static inline bool full_text_search_string(FTS_INDEX *fts, SIMPLE_PATTERN *q, STRING *ptr) {
    fts->searches++;
    fts->string_searches++;
    return simple_pattern_matches_string(q, ptr);
}

static inline bool full_text_search_char(FTS_INDEX *fts, SIMPLE_PATTERN *q, char *ptr) {
    fts->searches++;
    fts->char_searches++;
    return simple_pattern_matches(q, ptr);
}

struct rrdcontext_to_json_v2_data {
    BUFFER *wb;
    struct api_v2_contexts_request *request;
    DICTIONARY *ctx;

    CONTEXTS_V2_OPTIONS options;
    uint64_t hard_hash;
    uint64_t soft_hash;

    struct {
        SIMPLE_PATTERN *scope_pattern;
        SIMPLE_PATTERN *pattern;
    } nodes;

    struct {
        SIMPLE_PATTERN *scope_pattern;
        SIMPLE_PATTERN *pattern;
    } contexts;

    struct {
        FTS_MATCH host_match;
        char host_uuid_buffer[UUID_STR_LEN];
        SIMPLE_PATTERN *pattern;
        FTS_INDEX fts;
    } q;
};

static FTS_MATCH rrdcontext_to_json_v2_full_text_search(struct rrdcontext_to_json_v2_data *ctl, RRDCONTEXT *rc, SIMPLE_PATTERN *q) {
    if(unlikely(full_text_search_string(&ctl->q.fts, q, rc->id) ||
                full_text_search_string(&ctl->q.fts, q, rc->family)))
        return FTS_MATCHED_CONTEXT;

    if(unlikely(full_text_search_string(&ctl->q.fts, q, rc->title)))
        return FTS_MATCHED_TITLE;

    if(unlikely(full_text_search_string(&ctl->q.fts, q, rc->units)))
        return FTS_MATCHED_UNITS;

    FTS_MATCH matched = FTS_MATCHED_NONE;
    RRDINSTANCE *ri;
    dfe_start_read(rc->rrdinstances, ri) {
        if(matched) break;

        if(unlikely(full_text_search_string(&ctl->q.fts, q, ri->id)) ||
           (ri->name != ri->id && full_text_search_string(&ctl->q.fts, q, ri->name))) {
            matched = FTS_MATCHED_INSTANCE;
            break;
        }

        RRDMETRIC *rm;
        dfe_start_read(ri->rrdmetrics, rm) {
            if(unlikely(full_text_search_string(&ctl->q.fts, q, rm->id)) ||
               (rm->name != rm->id && full_text_search_string(&ctl->q.fts, q, rm->name))) {
                matched = FTS_MATCHED_DIMENSION;
                break;
            }
        }
        dfe_done(rm);

        size_t label_searches = 0;
        if(unlikely(ri->rrdlabels && dictionary_entries(ri->rrdlabels) &&
                    rrdlabels_match_simple_pattern_parsed(ri->rrdlabels, q, ':', &label_searches))) {
            ctl->q.fts.searches += label_searches;
            ctl->q.fts.char_searches += label_searches;
            matched = FTS_MATCHED_LABEL;
            break;
        }
        ctl->q.fts.searches += label_searches;
        ctl->q.fts.char_searches += label_searches;

        if(ri->rrdset) {
            RRDSET *st = ri->rrdset;
            netdata_rwlock_rdlock(&st->alerts.rwlock);
            for (RRDCALC *rcl = st->alerts.base; rcl; rcl = rcl->next) {
                if(unlikely(full_text_search_string(&ctl->q.fts, q, rcl->name))) {
                    matched = FTS_MATCHED_ALERT;
                    break;
                }

                if(unlikely(full_text_search_string(&ctl->q.fts, q, rcl->info))) {
                    matched = FTS_MATCHED_ALERT_INFO;
                    break;
                }
            }
            netdata_rwlock_unlock(&st->alerts.rwlock);
        }
    }
    dfe_done(ri);
    return matched;
}

static bool rrdcontext_to_json_v2_add_context(void *data, RRDCONTEXT_ACQUIRED *rca, bool queryable_context __maybe_unused) {
    struct rrdcontext_to_json_v2_data *ctl = data;

    RRDCONTEXT *rc = rrdcontext_acquired_value(rca);

    FTS_MATCH match = ctl->q.host_match;
    if((ctl->options & CONTEXTS_V2_SEARCH) && ctl->q.pattern) {
        match = rrdcontext_to_json_v2_full_text_search(ctl, rc, ctl->q.pattern);

        if(match == FTS_MATCHED_NONE)
            return false;
    }

    struct rrdcontext_to_json_v2_entry t = {
            .count = 0,
            .id = rc->id,
            .family = rc->family,
            .priority = rc->priority,
            .first_time_s = rc->first_time_s,
            .last_time_s = rc->last_time_s,
            .flags = rc->flags,
            .match = match,
    }, *z = dictionary_set(ctl->ctx, string2str(rc->id), &t, sizeof(t));

    if(!z->count) {
        // we just added this
        z->count = 1;
    }
    else {
        // it is already in there
        z->count++;
        z->flags |= rc->flags;

        if(z->priority > rc->priority)
            z->priority = rc->priority;

        if(z->first_time_s > rc->first_time_s)
            z->first_time_s = rc->first_time_s;

        if(z->last_time_s < rc->last_time_s)
            z->last_time_s = rc->last_time_s;
    }

    return true;
}

static bool rrdcontext_to_json_v2_add_host(void *data, RRDHOST *host, bool queryable_host) {
    if(!queryable_host || !host->rrdctx.contexts)
        // the host matches the 'scope_host' but does not match the 'host' patterns
        // or the host does not have any contexts
        return false;

    struct rrdcontext_to_json_v2_data *ctl = data;
    BUFFER *wb = ctl->wb;

    bool host_matched = (ctl->options & CONTEXTS_V2_NODES);
    bool do_contexts = (ctl->options & (CONTEXTS_V2_CONTEXTS | CONTEXTS_V2_SEARCH));

    ctl->q.host_match = FTS_MATCHED_NONE;
    if((ctl->options & CONTEXTS_V2_SEARCH)) {
        // check if we match the host itself
        if(ctl->q.pattern && (
                full_text_search_string(&ctl->q.fts, ctl->q.pattern, host->hostname) ||
                full_text_search_char(&ctl->q.fts, ctl->q.pattern, host->machine_guid) ||
                (ctl->q.pattern && full_text_search_char(&ctl->q.fts, ctl->q.pattern, ctl->q.host_uuid_buffer)))) {
            ctl->q.host_match = FTS_MATCHED_HOST;
            do_contexts = true;
        }
    }

    if(do_contexts) {
        // save it
        SIMPLE_PATTERN *old_q = ctl->q.pattern;

        if(ctl->q.host_match == FTS_MATCHED_HOST)
            // do not do pattern matching on contexts - we matched the host itself
            ctl->q.pattern = NULL;

        size_t added = query_scope_foreach_context(
                host, ctl->request->scope_contexts,
                ctl->contexts.scope_pattern, ctl->contexts.pattern,
                rrdcontext_to_json_v2_add_context, queryable_host, ctl);

        // restore it
        ctl->q.pattern = old_q;

        if(added)
            host_matched = true;
    }

    if(host_matched && (ctl->options & (CONTEXTS_V2_NODES | CONTEXTS_V2_DEBUG))) {
        buffer_json_add_array_item_object(wb);
        buffer_json_member_add_string(wb, "mg", host->machine_guid);
        buffer_json_member_add_uuid(wb, "nd", host->node_id);
        buffer_json_member_add_string(wb, "nm", rrdhost_hostname(host));
        buffer_json_object_close(wb);
    }

    return host_matched;
}

static void buffer_json_contexts_v2_options_to_array(BUFFER *wb, CONTEXTS_V2_OPTIONS options) {
    if(options & CONTEXTS_V2_DEBUG)
        buffer_json_add_array_item_string(wb, "debug");

    if(options & CONTEXTS_V2_NODES)
        buffer_json_add_array_item_string(wb, "nodes");

    if(options & CONTEXTS_V2_CONTEXTS)
        buffer_json_add_array_item_string(wb, "contexts");

    if(options & CONTEXTS_V2_SEARCH)
        buffer_json_add_array_item_string(wb, "search");
}

int rrdcontext_to_json_v2(BUFFER *wb, struct api_v2_contexts_request *req, CONTEXTS_V2_OPTIONS options) {
    req->timings.processing_ut = now_monotonic_usec();

    if(options & CONTEXTS_V2_SEARCH)
        options |= CONTEXTS_V2_CONTEXTS;

    struct rrdcontext_to_json_v2_data ctl = {
            .wb = wb,
            .request = req,
            .ctx = NULL,
            .options = options,
            .hard_hash = 0,
            .soft_hash = 0,
            .nodes.scope_pattern = string_to_simple_pattern(req->scope_nodes),
            .nodes.pattern = string_to_simple_pattern(req->nodes),
            .contexts.pattern = string_to_simple_pattern(req->contexts),
            .contexts.scope_pattern = string_to_simple_pattern(req->scope_contexts),
            .q.pattern = string_to_simple_pattern_nocase(req->q),
    };

    if(options & CONTEXTS_V2_CONTEXTS)
        ctl.ctx = dictionary_create_advanced(DICT_OPTION_SINGLE_THREADED | DICT_OPTION_DONT_OVERWRITE_VALUE | DICT_OPTION_FIXED_SIZE, NULL, sizeof(struct rrdcontext_to_json_v2_entry));

    time_t now_s = now_realtime_sec();
    buffer_json_initialize(wb, "\"", "\"", 0, true, false);

    if(options & CONTEXTS_V2_DEBUG) {
        buffer_json_member_add_object(wb, "agent");
        buffer_json_member_add_string(wb, "mg", localhost->machine_guid);
        buffer_json_member_add_uuid(wb, "nd", localhost->node_id);
        buffer_json_member_add_string(wb, "nm", rrdhost_hostname(localhost));
        if (req->q)
            buffer_json_member_add_string(wb, "q", req->q);
        buffer_json_member_add_time_t(wb, "now", now_s);
        buffer_json_object_close(wb);

        buffer_json_member_add_object(wb, "request");

        buffer_json_member_add_object(wb, "scope");
        buffer_json_member_add_string(wb, "scope_nodes", req->scope_nodes);
        buffer_json_member_add_string(wb, "scope_contexts", req->scope_contexts);
        buffer_json_object_close(wb);

        buffer_json_member_add_object(wb, "selectors");
        buffer_json_member_add_string(wb, "nodes", req->nodes);
        buffer_json_member_add_string(wb, "contexts", req->contexts);
        buffer_json_object_close(wb);

        buffer_json_member_add_string(wb, "q", req->q);
        buffer_json_member_add_array(wb, "options");
        buffer_json_contexts_v2_options_to_array(wb, options);
        buffer_json_array_close(wb);

        buffer_json_object_close(wb);
    }

    if(options & (CONTEXTS_V2_NODES | CONTEXTS_V2_DEBUG))
        buffer_json_member_add_array(wb, "nodes");

    query_scope_foreach_host(ctl.nodes.scope_pattern, ctl.nodes.pattern,
                                              rrdcontext_to_json_v2_add_host, &ctl, &ctl.hard_hash, &ctl.soft_hash,
                                              ctl.q.host_uuid_buffer);
    if(options & (CONTEXTS_V2_NODES | CONTEXTS_V2_DEBUG))
        buffer_json_array_close(wb);

    req->timings.output_ut = now_monotonic_usec();
    buffer_json_member_add_object(wb, "versions");
    buffer_json_member_add_uint64(wb, "contexts_hard_hash", ctl.hard_hash);
    buffer_json_member_add_uint64(wb, "contexts_soft_hash", ctl.soft_hash);
    buffer_json_object_close(wb);

    if(options & CONTEXTS_V2_CONTEXTS) {
        buffer_json_member_add_object(wb, "contexts");
        struct rrdcontext_to_json_v2_entry *z;
        dfe_start_read(ctl.ctx, z){
            bool collected = z->flags & RRD_FLAG_COLLECTED;

            buffer_json_member_add_object(wb, string2str(z->id));
            {
                buffer_json_member_add_string(wb, "family", string2str(z->family));
                buffer_json_member_add_uint64(wb, "priority", z->priority);
                buffer_json_member_add_time_t(wb, "first_entry", z->first_time_s);
                buffer_json_member_add_time_t(wb, "last_entry", collected ? now_s : z->last_time_s);
                buffer_json_member_add_boolean(wb, "live", collected);
                if (options & CONTEXTS_V2_SEARCH)
                    buffer_json_member_add_string(wb, "match", fts_match_to_string(z->match));
            }
            buffer_json_object_close(wb);
        }
        dfe_done(z);
        buffer_json_object_close(wb); // contexts
    }

    if(options & CONTEXTS_V2_SEARCH) {
        buffer_json_member_add_object(wb, "searches");
        buffer_json_member_add_uint64(wb, "strings", ctl.q.fts.string_searches);
        buffer_json_member_add_uint64(wb, "char", ctl.q.fts.char_searches);
        buffer_json_member_add_uint64(wb, "total", ctl.q.fts.searches);
        buffer_json_object_close(wb);
    }

    req->timings.finished_ut = now_monotonic_usec();
    buffer_json_member_add_object(wb, "timings");
    buffer_json_member_add_double(wb, "prep_ms", (NETDATA_DOUBLE)(req->timings.processing_ut - req->timings.received_ut) / USEC_PER_MS);
    buffer_json_member_add_double(wb, "query_ms", (NETDATA_DOUBLE)(req->timings.output_ut - req->timings.processing_ut) / USEC_PER_MS);
    buffer_json_member_add_double(wb, "output_ms", (NETDATA_DOUBLE)(req->timings.finished_ut - req->timings.output_ut) / USEC_PER_MS);
    buffer_json_member_add_double(wb, "total_ms", (NETDATA_DOUBLE)(req->timings.finished_ut - req->timings.received_ut) / USEC_PER_MS);
    buffer_json_object_close(wb);
    buffer_json_finalize(wb);

    dictionary_destroy(ctl.ctx);
    simple_pattern_free(ctl.nodes.scope_pattern);
    simple_pattern_free(ctl.nodes.pattern);
    simple_pattern_free(ctl.contexts.pattern);
    simple_pattern_free(ctl.contexts.scope_pattern);
    simple_pattern_free(ctl.q.pattern);

    return HTTP_RESP_OK;
}

