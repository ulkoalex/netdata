// SPDX-License-Identifier: GPL-3.0-or-later

#include "query.h"
#include "web/api/formatters/rrd2json.h"
#include "rrdr.h"

#include "average/average.h"
#include "countif/countif.h"
#include "incremental_sum/incremental_sum.h"
#include "max/max.h"
#include "median/median.h"
#include "min/min.h"
#include "sum/sum.h"
#include "stddev/stddev.h"
#include "ses/ses.h"
#include "des/des.h"
#include "percentile/percentile.h"
#include "trimmed_mean/trimmed_mean.h"

#define POINTS_TO_EXPAND_QUERY 5

// ----------------------------------------------------------------------------

static struct {
    const char *name;
    uint32_t hash;
    RRDR_TIME_GROUPING value;

    // One time initialization for the module.
    // This is called once, when netdata starts.
    void (*init)(void);

    // Allocate all required structures for a query.
    // This is called once for each netdata query.
    void (*create)(struct rrdresult *r, const char *options);

    // Cleanup collected values, but don't destroy the structures.
    // This is called when the query engine switches dimensions,
    // as part of the same query (so same chart, switching metric).
    void (*reset)(struct rrdresult *r);

    // Free all resources allocated for the query.
    void (*free)(struct rrdresult *r);

    // Add a single value into the calculation.
    // The module may decide to cache it, or use it in the fly.
    void (*add)(struct rrdresult *r, NETDATA_DOUBLE value);

    // Generate a single result for the values added so far.
    // More values and points may be requested later.
    // It is up to the module to reset its internal structures
    // when flushing it (so for a few modules it may be better to
    // continue after a flush as if nothing changed, for others a
    // cleanup of the internal structures may be required).
    NETDATA_DOUBLE (*flush)(struct rrdresult *r, RRDR_VALUE_FLAGS *rrdr_value_options_ptr);

    TIER_QUERY_FETCH tier_query_fetch;
} api_v1_data_groups[] = {
        {.name = "average",
                .hash  = 0,
                .value = RRDR_GROUPING_AVERAGE,
                .init  = NULL,
                .create= grouping_create_average,
                .reset = grouping_reset_average,
                .free  = grouping_free_average,
                .add   = grouping_add_average,
                .flush = grouping_flush_average,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "avg",                             // alias on 'average'
                .hash  = 0,
                .value = RRDR_GROUPING_AVERAGE,
                .init  = NULL,
                .create= grouping_create_average,
                .reset = grouping_reset_average,
                .free  = grouping_free_average,
                .add   = grouping_add_average,
                .flush = grouping_flush_average,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "mean",                            // alias on 'average'
                .hash  = 0,
                .value = RRDR_GROUPING_AVERAGE,
                .init  = NULL,
                .create= grouping_create_average,
                .reset = grouping_reset_average,
                .free  = grouping_free_average,
                .add   = grouping_add_average,
                .flush = grouping_flush_average,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean1",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN1,
                .init  = NULL,
                .create= grouping_create_trimmed_mean1,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean2",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN2,
                .init  = NULL,
                .create= grouping_create_trimmed_mean2,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean3",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN3,
                .init  = NULL,
                .create= grouping_create_trimmed_mean3,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean5",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN5,
                .init  = NULL,
                .create= grouping_create_trimmed_mean5,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean10",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN10,
                .init  = NULL,
                .create= grouping_create_trimmed_mean10,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean15",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN15,
                .init  = NULL,
                .create= grouping_create_trimmed_mean15,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean20",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN20,
                .init  = NULL,
                .create= grouping_create_trimmed_mean20,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean25",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN25,
                .init  = NULL,
                .create= grouping_create_trimmed_mean25,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-mean",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEAN5,
                .init  = NULL,
                .create= grouping_create_trimmed_mean5,
                .reset = grouping_reset_trimmed_mean,
                .free  = grouping_free_trimmed_mean,
                .add   = grouping_add_trimmed_mean,
                .flush = grouping_flush_trimmed_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name  = "incremental_sum",
                .hash  = 0,
                .value = RRDR_GROUPING_INCREMENTAL_SUM,
                .init  = NULL,
                .create= grouping_create_incremental_sum,
                .reset = grouping_reset_incremental_sum,
                .free  = grouping_free_incremental_sum,
                .add   = grouping_add_incremental_sum,
                .flush = grouping_flush_incremental_sum,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "incremental-sum",
                .hash  = 0,
                .value = RRDR_GROUPING_INCREMENTAL_SUM,
                .init  = NULL,
                .create= grouping_create_incremental_sum,
                .reset = grouping_reset_incremental_sum,
                .free  = grouping_free_incremental_sum,
                .add   = grouping_add_incremental_sum,
                .flush = grouping_flush_incremental_sum,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "median",
                .hash  = 0,
                .value = RRDR_GROUPING_MEDIAN,
                .init  = NULL,
                .create= grouping_create_median,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median1",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN1,
                .init  = NULL,
                .create= grouping_create_trimmed_median1,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median2",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN2,
                .init  = NULL,
                .create= grouping_create_trimmed_median2,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median3",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN3,
                .init  = NULL,
                .create= grouping_create_trimmed_median3,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median5",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN5,
                .init  = NULL,
                .create= grouping_create_trimmed_median5,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median10",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN10,
                .init  = NULL,
                .create= grouping_create_trimmed_median10,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median15",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN15,
                .init  = NULL,
                .create= grouping_create_trimmed_median15,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median20",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN20,
                .init  = NULL,
                .create= grouping_create_trimmed_median20,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median25",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN25,
                .init  = NULL,
                .create= grouping_create_trimmed_median25,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "trimmed-median",
                .hash  = 0,
                .value = RRDR_GROUPING_TRIMMED_MEDIAN5,
                .init  = NULL,
                .create= grouping_create_trimmed_median5,
                .reset = grouping_reset_median,
                .free  = grouping_free_median,
                .add   = grouping_add_median,
                .flush = grouping_flush_median,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile25",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE25,
                .init  = NULL,
                .create= grouping_create_percentile25,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile50",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE50,
                .init  = NULL,
                .create= grouping_create_percentile50,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile75",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE75,
                .init  = NULL,
                .create= grouping_create_percentile75,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile80",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE80,
                .init  = NULL,
                .create= grouping_create_percentile80,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile90",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE90,
                .init  = NULL,
                .create= grouping_create_percentile90,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile95",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE95,
                .init  = NULL,
                .create= grouping_create_percentile95,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile97",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE97,
                .init  = NULL,
                .create= grouping_create_percentile97,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile98",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE98,
                .init  = NULL,
                .create= grouping_create_percentile98,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile99",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE99,
                .init  = NULL,
                .create= grouping_create_percentile99,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "percentile",
                .hash  = 0,
                .value = RRDR_GROUPING_PERCENTILE95,
                .init  = NULL,
                .create= grouping_create_percentile95,
                .reset = grouping_reset_percentile,
                .free  = grouping_free_percentile,
                .add   = grouping_add_percentile,
                .flush = grouping_flush_percentile,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "min",
                .hash  = 0,
                .value = RRDR_GROUPING_MIN,
                .init  = NULL,
                .create= grouping_create_min,
                .reset = grouping_reset_min,
                .free  = grouping_free_min,
                .add   = grouping_add_min,
                .flush = grouping_flush_min,
                .tier_query_fetch = TIER_QUERY_FETCH_MIN
        },
        {.name = "max",
                .hash  = 0,
                .value = RRDR_GROUPING_MAX,
                .init  = NULL,
                .create= grouping_create_max,
                .reset = grouping_reset_max,
                .free  = grouping_free_max,
                .add   = grouping_add_max,
                .flush = grouping_flush_max,
                .tier_query_fetch = TIER_QUERY_FETCH_MAX
        },
        {.name = "sum",
                .hash  = 0,
                .value = RRDR_GROUPING_SUM,
                .init  = NULL,
                .create= grouping_create_sum,
                .reset = grouping_reset_sum,
                .free  = grouping_free_sum,
                .add   = grouping_add_sum,
                .flush = grouping_flush_sum,
                .tier_query_fetch = TIER_QUERY_FETCH_SUM
        },

        // standard deviation
        {.name = "stddev",
                .hash  = 0,
                .value = RRDR_GROUPING_STDDEV,
                .init  = NULL,
                .create= grouping_create_stddev,
                .reset = grouping_reset_stddev,
                .free  = grouping_free_stddev,
                .add   = grouping_add_stddev,
                .flush = grouping_flush_stddev,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "cv",                           // coefficient variation is calculated by stddev
                .hash  = 0,
                .value = RRDR_GROUPING_CV,
                .init  = NULL,
                .create= grouping_create_stddev, // not an error, stddev calculates this too
                .reset = grouping_reset_stddev,  // not an error, stddev calculates this too
                .free  = grouping_free_stddev,   // not an error, stddev calculates this too
                .add   = grouping_add_stddev,    // not an error, stddev calculates this too
                .flush = grouping_flush_coefficient_of_variation,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "rsd",                          // alias of 'cv'
                .hash  = 0,
                .value = RRDR_GROUPING_CV,
                .init  = NULL,
                .create= grouping_create_stddev, // not an error, stddev calculates this too
                .reset = grouping_reset_stddev,  // not an error, stddev calculates this too
                .free  = grouping_free_stddev,   // not an error, stddev calculates this too
                .add   = grouping_add_stddev,    // not an error, stddev calculates this too
                .flush = grouping_flush_coefficient_of_variation,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },

        /*
        {.name = "mean",                        // same as average, no need to define it again
                .hash  = 0,
                .value = RRDR_GROUPING_MEAN,
                .setup = NULL,
                .create= grouping_create_stddev,
                .reset = grouping_reset_stddev,
                .free  = grouping_free_stddev,
                .add   = grouping_add_stddev,
                .flush = grouping_flush_mean,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        */

        /*
        {.name = "variance",                    // meaningless to offer
                .hash  = 0,
                .value = RRDR_GROUPING_VARIANCE,
                .setup = NULL,
                .create= grouping_create_stddev,
                .reset = grouping_reset_stddev,
                .free  = grouping_free_stddev,
                .add   = grouping_add_stddev,
                .flush = grouping_flush_variance,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        */

        // single exponential smoothing
        {.name = "ses",
                .hash  = 0,
                .value = RRDR_GROUPING_SES,
                .init  = grouping_init_ses,
                .create= grouping_create_ses,
                .reset = grouping_reset_ses,
                .free  = grouping_free_ses,
                .add   = grouping_add_ses,
                .flush = grouping_flush_ses,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "ema",                         // alias for 'ses'
                .hash  = 0,
                .value = RRDR_GROUPING_SES,
                .init  = NULL,
                .create= grouping_create_ses,
                .reset = grouping_reset_ses,
                .free  = grouping_free_ses,
                .add   = grouping_add_ses,
                .flush = grouping_flush_ses,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },
        {.name = "ewma",                        // alias for ses
                .hash  = 0,
                .value = RRDR_GROUPING_SES,
                .init  = NULL,
                .create= grouping_create_ses,
                .reset = grouping_reset_ses,
                .free  = grouping_free_ses,
                .add   = grouping_add_ses,
                .flush = grouping_flush_ses,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },

        // double exponential smoothing
        {.name = "des",
                .hash  = 0,
                .value = RRDR_GROUPING_DES,
                .init  = grouping_init_des,
                .create= grouping_create_des,
                .reset = grouping_reset_des,
                .free  = grouping_free_des,
                .add   = grouping_add_des,
                .flush = grouping_flush_des,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },

        {.name = "countif",
                .hash  = 0,
                .value = RRDR_GROUPING_COUNTIF,
                .init = NULL,
                .create= grouping_create_countif,
                .reset = grouping_reset_countif,
                .free  = grouping_free_countif,
                .add   = grouping_add_countif,
                .flush = grouping_flush_countif,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        },

        // terminator
        {.name = NULL,
                .hash  = 0,
                .value = RRDR_GROUPING_UNDEFINED,
                .init = NULL,
                .create= grouping_create_average,
                .reset = grouping_reset_average,
                .free  = grouping_free_average,
                .add   = grouping_add_average,
                .flush = grouping_flush_average,
                .tier_query_fetch = TIER_QUERY_FETCH_AVERAGE
        }
};

void time_grouping_init(void) {
    int i;

    for(i = 0; api_v1_data_groups[i].name ; i++) {
        api_v1_data_groups[i].hash = simple_hash(api_v1_data_groups[i].name);

        if(api_v1_data_groups[i].init)
            api_v1_data_groups[i].init();
    }
}

const char *time_grouping_method2string(RRDR_TIME_GROUPING group) {
    int i;

    for(i = 0; api_v1_data_groups[i].name ; i++) {
        if(api_v1_data_groups[i].value == group) {
            return api_v1_data_groups[i].name;
        }
    }

    return "unknown-group-method";
}

RRDR_TIME_GROUPING time_grouping_parse(const char *name, RRDR_TIME_GROUPING def) {
    int i;

    uint32_t hash = simple_hash(name);
    for(i = 0; api_v1_data_groups[i].name ; i++)
        if(unlikely(hash == api_v1_data_groups[i].hash && !strcmp(name, api_v1_data_groups[i].name)))
            return api_v1_data_groups[i].value;

    return def;
}

const char *time_grouping_tostring(RRDR_TIME_GROUPING group) {
    int i;

    for(i = 0; api_v1_data_groups[i].name ; i++)
        if(unlikely(group == api_v1_data_groups[i].value))
            return api_v1_data_groups[i].name;

    return "unknown";
}

static void rrdr_set_grouping_function(RRDR *r, RRDR_TIME_GROUPING group_method) {
    int i, found = 0;
    for(i = 0; !found && api_v1_data_groups[i].name ;i++) {
        if(api_v1_data_groups[i].value == group_method) {
            r->time_grouping.create  = api_v1_data_groups[i].create;
            r->time_grouping.reset   = api_v1_data_groups[i].reset;
            r->time_grouping.free    = api_v1_data_groups[i].free;
            r->time_grouping.add     = api_v1_data_groups[i].add;
            r->time_grouping.flush   = api_v1_data_groups[i].flush;
            r->time_grouping.tier_query_fetch = api_v1_data_groups[i].tier_query_fetch;
            found = 1;
        }
    }
    if(!found) {
        errno = 0;
        internal_error(true, "QUERY: grouping method %u not found. Using 'average'", (unsigned int)group_method);
        r->time_grouping.create  = grouping_create_average;
        r->time_grouping.reset   = grouping_reset_average;
        r->time_grouping.free    = grouping_free_average;
        r->time_grouping.add     = grouping_add_average;
        r->time_grouping.flush   = grouping_flush_average;
        r->time_grouping.tier_query_fetch = TIER_QUERY_FETCH_AVERAGE;
    }
}

RRDR_GROUP_BY group_by_parse(char *s) {
    RRDR_GROUP_BY group_by = RRDR_GROUP_BY_NONE;

    while(s) {
        char *key = mystrsep(&s, ",| ");
        if (!key || !*key) continue;

        if (strcmp(key, "selected") == 0)
            group_by |= RRDR_GROUP_BY_SELECTED;

        if (strcmp(key, "dimension") == 0)
            group_by |= RRDR_GROUP_BY_DIMENSION;

        if (strcmp(key, "instance") == 0)
            group_by |= RRDR_GROUP_BY_INSTANCE;

        if (strcmp(key, "label") == 0)
            group_by |= RRDR_GROUP_BY_LABEL;

        if (strcmp(key, "node") == 0)
            group_by |= RRDR_GROUP_BY_NODE;

        if (strcmp(key, "context") == 0)
            group_by |= RRDR_GROUP_BY_CONTEXT;

        if (strcmp(key, "units") == 0)
            group_by |= RRDR_GROUP_BY_UNITS;
    }

    return group_by;
}

void buffer_json_group_by_to_array(BUFFER *wb, RRDR_GROUP_BY group_by) {
    if(group_by & RRDR_GROUP_BY_SELECTED)
        buffer_json_add_array_item_string(wb, "selected");

    if(group_by & RRDR_GROUP_BY_DIMENSION)
        buffer_json_add_array_item_string(wb, "dimension");

    if(group_by & RRDR_GROUP_BY_INSTANCE)
        buffer_json_add_array_item_string(wb, "instance");

    if(group_by & RRDR_GROUP_BY_LABEL)
        buffer_json_add_array_item_string(wb, "label");

    if(group_by & RRDR_GROUP_BY_NODE)
        buffer_json_add_array_item_string(wb, "node");

    if(group_by & RRDR_GROUP_BY_CONTEXT)
        buffer_json_add_array_item_string(wb, "context");

    if(group_by & RRDR_GROUP_BY_UNITS)
        buffer_json_add_array_item_string(wb, "units");
}

RRDR_GROUP_BY_FUNCTION group_by_aggregate_function_parse(const char *s) {
    if(strcmp(s, "average") == 0)
        return RRDR_GROUP_BY_FUNCTION_AVERAGE;

    if(strcmp(s, "avg") == 0)
        return RRDR_GROUP_BY_FUNCTION_AVERAGE;

    if(strcmp(s, "min") == 0)
        return RRDR_GROUP_BY_FUNCTION_MIN;

    if(strcmp(s, "max") == 0)
        return RRDR_GROUP_BY_FUNCTION_MAX;

    if(strcmp(s, "sum") == 0)
        return RRDR_GROUP_BY_FUNCTION_SUM;

    return RRDR_GROUP_BY_FUNCTION_AVERAGE;
}

const char *group_by_aggregate_function_to_string(RRDR_GROUP_BY_FUNCTION group_by_function) {
    switch(group_by_function) {
        default:
        case RRDR_GROUP_BY_FUNCTION_AVERAGE:
            return "average";

        case RRDR_GROUP_BY_FUNCTION_MIN:
            return "min";

        case RRDR_GROUP_BY_FUNCTION_MAX:
            return "max";

        case RRDR_GROUP_BY_FUNCTION_SUM:
            return "sum";
    }
}

// ----------------------------------------------------------------------------
// helpers to find our way in RRDR

static inline RRDR_VALUE_FLAGS *UNUSED_FUNCTION(rrdr_line_options)(RRDR *r, long rrdr_line) {
    return &r->o[ rrdr_line * r->d ];
}

static inline NETDATA_DOUBLE *UNUSED_FUNCTION(rrdr_line_values)(RRDR *r, long rrdr_line) {
    return &r->v[ rrdr_line * r->d ];
}

static inline long rrdr_line_init(RRDR *r, time_t t, long rrdr_line) {
    rrdr_line++;

    internal_error(rrdr_line >= (long)r->n,
                   "QUERY: requested to step above RRDR size for query '%s'",
                   r->internal.qt->id);

    internal_error(r->t[rrdr_line] != 0 && r->t[rrdr_line] != t,
                   "QUERY: overwriting the timestamp of RRDR line %zu from %zu to %zu, of query '%s'",
                   (size_t)rrdr_line, (size_t)r->t[rrdr_line], (size_t)t, r->internal.qt->id);

    // save the time
    r->t[rrdr_line] = t;

    return rrdr_line;
}

// ----------------------------------------------------------------------------
// tier management

static bool query_metric_is_valid_tier(QUERY_METRIC *qm, size_t tier) {
    if(!qm->tiers[tier].db_metric_handle || !qm->tiers[tier].db_first_time_s || !qm->tiers[tier].db_last_time_s || !qm->tiers[tier].db_update_every_s)
        return false;

    return true;
}

static size_t query_metric_first_working_tier(QUERY_METRIC *qm) {
    for(size_t tier = 0; tier < storage_tiers ; tier++) {

        // find the db time-range for this tier for all metrics
        STORAGE_METRIC_HANDLE *db_metric_handle = qm->tiers[tier].db_metric_handle;
        time_t first_time_s = qm->tiers[tier].db_first_time_s;
        time_t last_time_s  = qm->tiers[tier].db_last_time_s;
        time_t update_every_s = qm->tiers[tier].db_update_every_s;

        if(!db_metric_handle || !first_time_s || !last_time_s || !update_every_s)
            continue;

        return tier;
    }

    return 0;
}

static long query_plan_points_coverage_weight(time_t db_first_time_s, time_t db_last_time_s, time_t db_update_every_s, time_t after_wanted, time_t before_wanted, size_t points_wanted, size_t tier __maybe_unused) {
    if(db_first_time_s == 0 ||
        db_last_time_s == 0 ||
        db_update_every_s == 0 ||
        db_first_time_s > before_wanted ||
        db_last_time_s < after_wanted)
        return -LONG_MAX;

    long long common_first_t = MAX(db_first_time_s, after_wanted);
    long long common_last_t = MIN(db_last_time_s, before_wanted);

    long long time_coverage = (common_last_t - common_first_t) * 1000000LL / (before_wanted - after_wanted);
    long long points_wanted_in_coverage = (long long)points_wanted * time_coverage / 1000000LL;

    long long points_available = (common_last_t - common_first_t) / db_update_every_s;
    long long points_delta = (long)(points_available - points_wanted_in_coverage);
    long long points_coverage = (points_delta < 0) ? (long)(points_available * time_coverage / points_wanted_in_coverage) : time_coverage;

    // a way to benefit higher tiers
    // points_coverage += (long)tier * 10000;

    if(points_available <= 0)
        return -LONG_MAX;

    return (long)(points_coverage + (25000LL * tier)); // 2.5% benefit for each higher tier
}

static size_t query_metric_best_tier_for_timeframe(QUERY_METRIC *qm, time_t after_wanted, time_t before_wanted, size_t points_wanted) {
    if(unlikely(storage_tiers < 2))
        return 0;

    if(unlikely(after_wanted == before_wanted || points_wanted <= 0))
        return query_metric_first_working_tier(qm);

    time_t min_first_time_s = 0;
    time_t max_last_time_s = 0;

    for(size_t tier = 0; tier < storage_tiers ; tier++) {
        time_t first_time_s = qm->tiers[tier].db_first_time_s;
        time_t last_time_s  = qm->tiers[tier].db_last_time_s;

        if(!min_first_time_s || (first_time_s && first_time_s < min_first_time_s))
            min_first_time_s = first_time_s;

        if(!max_last_time_s || (last_time_s && last_time_s > max_last_time_s))
            max_last_time_s = last_time_s;
    }

    for(size_t tier = 0; tier < storage_tiers ; tier++) {

        // find the db time-range for this tier for all metrics
        STORAGE_METRIC_HANDLE *db_metric_handle = qm->tiers[tier].db_metric_handle;
        time_t first_time_s = qm->tiers[tier].db_first_time_s;
        time_t last_time_s  = qm->tiers[tier].db_last_time_s;
        time_t update_every_s = qm->tiers[tier].db_update_every_s;

        if( !db_metric_handle ||
            !first_time_s ||
            !last_time_s ||
            !update_every_s ||
            first_time_s > before_wanted ||
            last_time_s < after_wanted
            ) {
            qm->tiers[tier].weight = -LONG_MAX;
            continue;
        }

        internal_fatal(first_time_s > before_wanted || last_time_s < after_wanted, "QUERY: invalid db durations");

        qm->tiers[tier].weight = query_plan_points_coverage_weight(
                min_first_time_s, max_last_time_s, update_every_s,
                after_wanted, before_wanted, points_wanted, tier);
    }

    size_t best_tier = 0;
    for(size_t tier = 1; tier < storage_tiers ; tier++) {
        if(qm->tiers[tier].weight >= qm->tiers[best_tier].weight)
            best_tier = tier;
    }

    return best_tier;
}

static size_t rrddim_find_best_tier_for_timeframe(QUERY_TARGET *qt, time_t after_wanted, time_t before_wanted, size_t points_wanted) {
    if(unlikely(storage_tiers < 2))
        return 0;

    if(unlikely(after_wanted == before_wanted || points_wanted <= 0)) {
        internal_error(true, "QUERY: '%s' has invalid params to tier calculation", qt->id);
        return 0;
    }

    long weight[storage_tiers];

    for(size_t tier = 0; tier < storage_tiers ; tier++) {

        time_t common_first_time_s = 0;
        time_t common_last_time_s = 0;
        time_t common_update_every_s = 0;

        // find the db time-range for this tier for all metrics
        for(size_t i = 0, used = qt->query.used; i < used ; i++) {
            QUERY_METRIC *qm = query_metric(qt, i);

            time_t first_time_s = qm->tiers[tier].db_first_time_s;
            time_t last_time_s  = qm->tiers[tier].db_last_time_s;
            time_t update_every_s = qm->tiers[tier].db_update_every_s;

            if(!first_time_s || !last_time_s || !update_every_s)
                continue;

            if(!common_first_time_s)
                common_first_time_s = first_time_s;
            else
                common_first_time_s = MIN(first_time_s, common_first_time_s);

            if(!common_last_time_s)
                common_last_time_s = last_time_s;
            else
                common_last_time_s = MAX(last_time_s, common_last_time_s);

            if(!common_update_every_s)
                common_update_every_s = update_every_s;
            else
                common_update_every_s = MIN(update_every_s, common_update_every_s);
        }

        weight[tier] = query_plan_points_coverage_weight(common_first_time_s, common_last_time_s, common_update_every_s, after_wanted, before_wanted, points_wanted, tier);
    }

    size_t best_tier = 0;
    for(size_t tier = 1; tier < storage_tiers ; tier++) {
        if(weight[tier] >= weight[best_tier])
            best_tier = tier;
    }

    if(weight[best_tier] == -LONG_MAX)
        best_tier = 0;

    return best_tier;
}

static time_t rrdset_find_natural_update_every_for_timeframe(QUERY_TARGET *qt, time_t after_wanted, time_t before_wanted, size_t points_wanted, RRDR_OPTIONS options, size_t tier) {
    size_t best_tier;
    if((options & RRDR_OPTION_SELECTED_TIER) && tier < storage_tiers)
        best_tier = tier;
    else
        best_tier = rrddim_find_best_tier_for_timeframe(qt, after_wanted, before_wanted, points_wanted);

    // find the db minimum update every for this tier for all metrics
    time_t common_update_every_s = default_rrd_update_every;
    for(size_t i = 0, used = qt->query.used; i < used ; i++) {
        QUERY_METRIC *qm = query_metric(qt, i);

        time_t update_every_s = qm->tiers[best_tier].db_update_every_s;

        if(!i)
            common_update_every_s = update_every_s;
        else
            common_update_every_s = MIN(update_every_s, common_update_every_s);
    }

    return common_update_every_s;
}

// ----------------------------------------------------------------------------
// query ops

typedef struct query_point {
    time_t end_time;
    time_t start_time;
    NETDATA_DOUBLE value;
    size_t anomaly_outlier_points;
    size_t anomaly_all_points;
    SN_FLAGS flags;
#ifdef NETDATA_INTERNAL_CHECKS
    size_t id;
#endif
} QUERY_POINT;

QUERY_POINT QUERY_POINT_EMPTY = {
    .end_time = 0,
    .start_time = 0,
    .value = NAN,
    .anomaly_outlier_points = 0,
    .anomaly_all_points = 0,
    .flags = SN_FLAG_NONE,
#ifdef NETDATA_INTERNAL_CHECKS
    .id = 0,
#endif
};

#ifdef NETDATA_INTERNAL_CHECKS
#define query_point_set_id(point, point_id) (point).id = point_id
#else
#define query_point_set_id(point, point_id) debug_dummy()
#endif

typedef struct query_engine_ops {
    // configuration
    RRDR *r;
    QUERY_METRIC *qm;
    time_t view_update_every;
    time_t query_granularity;
    TIER_QUERY_FETCH tier_query_fetch;

    // query planer
    size_t current_plan;
    time_t current_plan_expire_time;
    time_t plan_expanded_after;
    time_t plan_expanded_before;

    // storage queries
    size_t tier;
    struct query_metric_tier *tier_ptr;
    struct storage_engine_query_handle *handle;
    STORAGE_POINT (*next_metric)(struct storage_engine_query_handle *handle);
    int (*is_finished)(struct storage_engine_query_handle *handle);
    void (*finalize)(struct storage_engine_query_handle *handle);

    // aggregating points over time
    void (*grouping_add)(struct rrdresult *r, NETDATA_DOUBLE value);
    NETDATA_DOUBLE (*grouping_flush)(struct rrdresult *r, RRDR_VALUE_FLAGS *rrdr_value_options_ptr);
    size_t group_points_non_zero;
    size_t group_points_added;
    size_t group_anomaly_outlier_points;
    size_t group_anomaly_all_points;
    RRDR_VALUE_FLAGS group_value_flags;

    // statistics
    size_t db_total_points_read;
    size_t db_points_read_per_tier[RRD_STORAGE_TIERS];

    struct {
        time_t expanded_after;
        time_t expanded_before;
        struct storage_engine_query_handle handle;
        STORAGE_POINT (*next_metric)(struct storage_engine_query_handle *handle);
        int (*is_finished)(struct storage_engine_query_handle *handle);
        void (*finalize)(struct storage_engine_query_handle *handle);
        bool initialized;
        bool finalized;
    } plans[QUERY_PLANS_MAX];

    struct query_engine_ops *next;
} QUERY_ENGINE_OPS;


// ----------------------------------------------------------------------------
// query planer

#define query_plan_should_switch_plan(ops, now) ((now) >= (ops)->current_plan_expire_time)

static size_t query_planer_expand_duration_in_points(time_t this_update_every, time_t next_update_every) {

    time_t delta = this_update_every - next_update_every;
    if(delta < 0) delta = -delta;

    size_t points;
    if(delta < this_update_every * POINTS_TO_EXPAND_QUERY)
        points = POINTS_TO_EXPAND_QUERY;
    else
        points = (delta + this_update_every - 1) / this_update_every;

    return points;
}

static void query_planer_initialize_plans(QUERY_ENGINE_OPS *ops) {
    QUERY_METRIC *qm = ops->qm;

    for(size_t p = 0; p < qm->plan.used ; p++) {
        size_t tier = qm->plan.array[p].tier;
        time_t update_every = qm->tiers[tier].db_update_every_s;

        size_t points_to_add_to_after;
        if(p > 0) {
            // there is another plan before to this

            size_t tier0 = qm->plan.array[p - 1].tier;
            time_t update_every0 = qm->tiers[tier0].db_update_every_s;

            points_to_add_to_after = query_planer_expand_duration_in_points(update_every, update_every0);
        }
        else
            points_to_add_to_after = (tier == 0) ? 0 : POINTS_TO_EXPAND_QUERY;

        size_t points_to_add_to_before;
        if(p + 1 < qm->plan.used) {
            // there is another plan after to this

            size_t tier1 = qm->plan.array[p+1].tier;
            time_t update_every1 = qm->tiers[tier1].db_update_every_s;

            points_to_add_to_before = query_planer_expand_duration_in_points(update_every, update_every1);
        }
        else
            points_to_add_to_before = POINTS_TO_EXPAND_QUERY;

        time_t after = qm->plan.array[p].after - (time_t)(update_every * points_to_add_to_after);
        time_t before = qm->plan.array[p].before + (time_t)(update_every * points_to_add_to_before);

        ops->plans[p].expanded_after = after;
        ops->plans[p].expanded_before = before;

        ops->r->internal.qt->db.tiers[tier].queries++;

        struct query_metric_tier *tier_ptr = &qm->tiers[tier];
        STORAGE_ENGINE *eng = query_metric_storage_engine(ops->r->internal.qt, qm, tier);
        eng->api.query_ops.init(
                tier_ptr->db_metric_handle,
                &ops->plans[p].handle,
                after, before,
                ops->r->internal.qt->request.priority);

        ops->plans[p].next_metric = eng->api.query_ops.next_metric;
        ops->plans[p].is_finished = eng->api.query_ops.is_finished;
        ops->plans[p].finalize = eng->api.query_ops.finalize;
        ops->plans[p].initialized = true;
        ops->plans[p].finalized = false;
    }
}

static void query_planer_finalize_plan(QUERY_ENGINE_OPS *ops, size_t plan_id) {
    // QUERY_METRIC *qm = ops->qm;

    if(ops->plans[plan_id].initialized && !ops->plans[plan_id].finalized) {
        ops->plans[plan_id].finalize(&ops->plans[plan_id].handle);
        ops->plans[plan_id].initialized = false;
        ops->plans[plan_id].finalized = true;
        ops->plans[plan_id].next_metric = NULL;
        ops->plans[plan_id].is_finished = NULL;
        ops->plans[plan_id].finalize = NULL;

        if(ops->current_plan == plan_id) {
            ops->next_metric = NULL;
            ops->is_finished = NULL;
            ops->finalize = NULL;
        }
    }
}

static void query_planer_finalize_remaining_plans(QUERY_ENGINE_OPS *ops) {
    QUERY_METRIC *qm = ops->qm;

    for(size_t p = 0; p < qm->plan.used ; p++)
        query_planer_finalize_plan(ops, p);
}

static void query_planer_activate_plan(QUERY_ENGINE_OPS *ops, size_t plan_id, time_t overwrite_after __maybe_unused) {
    QUERY_METRIC *qm = ops->qm;

    internal_fatal(plan_id >= qm->plan.used, "QUERY: invalid plan_id given");
    internal_fatal(!ops->plans[plan_id].initialized, "QUERY: plan has not been initialized");
    internal_fatal(ops->plans[plan_id].finalized, "QUERY: plan has been finalized");

    internal_fatal(qm->plan.array[plan_id].after > qm->plan.array[plan_id].before, "QUERY: flipped after/before");

    ops->tier = qm->plan.array[plan_id].tier;
    ops->tier_ptr = &qm->tiers[ops->tier];
    ops->handle = &ops->plans[plan_id].handle;
    ops->next_metric = ops->plans[plan_id].next_metric;
    ops->is_finished = ops->plans[plan_id].is_finished;
    ops->finalize = ops->plans[plan_id].finalize;
    ops->current_plan = plan_id;

    if(plan_id + 1 < qm->plan.used && qm->plan.array[plan_id + 1].after < qm->plan.array[plan_id].before)
        ops->current_plan_expire_time = qm->plan.array[plan_id + 1].after;
    else
        ops->current_plan_expire_time = qm->plan.array[plan_id].before;

    ops->plan_expanded_after = ops->plans[plan_id].expanded_after;
    ops->plan_expanded_before = ops->plans[plan_id].expanded_before;
}

static bool query_planer_next_plan(QUERY_ENGINE_OPS *ops, time_t now, time_t last_point_end_time) {
    QUERY_METRIC *qm = ops->qm;

    size_t old_plan = ops->current_plan;

    time_t next_plan_before_time;
    do {
        ops->current_plan++;

        if (ops->current_plan >= qm->plan.used) {
            ops->current_plan = old_plan;
            ops->current_plan_expire_time = ops->r->internal.qt->window.before;
            // let the query run with current plan
            // we will not switch it
            return false;
        }

        next_plan_before_time = qm->plan.array[ops->current_plan].before;
    } while(now >= next_plan_before_time || last_point_end_time >= next_plan_before_time);

    if(!query_metric_is_valid_tier(qm, qm->plan.array[ops->current_plan].tier)) {
        ops->current_plan = old_plan;
        ops->current_plan_expire_time = ops->r->internal.qt->window.before;
        return false;
    }

    query_planer_finalize_plan(ops, old_plan);
    query_planer_activate_plan(ops, ops->current_plan, MIN(now, last_point_end_time));
    return true;
}

static int compare_query_plan_entries_on_start_time(const void *a, const void *b) {
    QUERY_PLAN_ENTRY *p1 = (QUERY_PLAN_ENTRY *)a;
    QUERY_PLAN_ENTRY *p2 = (QUERY_PLAN_ENTRY *)b;
    return (p1->after < p2->after)?-1:1;
}

static bool query_plan(QUERY_ENGINE_OPS *ops, time_t after_wanted, time_t before_wanted, size_t points_wanted) {
    QUERY_METRIC *qm = ops->qm;

    // put our selected tier as the first plan
    size_t selected_tier;

    if(ops->r->view.options & RRDR_OPTION_SELECTED_TIER
       && ops->r->internal.qt->window.tier < storage_tiers
       && query_metric_is_valid_tier(qm, ops->r->internal.qt->window.tier)) {
        selected_tier = ops->r->internal.qt->window.tier;
    }
    else {
        selected_tier = query_metric_best_tier_for_timeframe(qm, after_wanted, before_wanted, points_wanted);

        if(ops->r->view.options & RRDR_OPTION_SELECTED_TIER)
            ops->r->view.options &= ~RRDR_OPTION_SELECTED_TIER;

        if(!query_metric_is_valid_tier(qm, selected_tier))
            return false;

        if(qm->tiers[selected_tier].db_first_time_s > before_wanted ||
           qm->tiers[selected_tier].db_last_time_s < after_wanted)
            return false;
    }

    qm->plan.used = 1;
    qm->plan.array[0].tier = selected_tier;
    qm->plan.array[0].after = (qm->tiers[selected_tier].db_first_time_s < after_wanted) ? after_wanted : qm->tiers[selected_tier].db_first_time_s;
    qm->plan.array[0].before = (qm->tiers[selected_tier].db_last_time_s > before_wanted) ? before_wanted : qm->tiers[selected_tier].db_last_time_s;

    if(!(ops->r->view.options & RRDR_OPTION_SELECTED_TIER)) {
        // the selected tier
        time_t selected_tier_first_time_s = qm->plan.array[0].after;
        time_t selected_tier_last_time_s = qm->plan.array[0].before;

        // check if our selected tier can start the query
        if (selected_tier_first_time_s > after_wanted) {
            // we need some help from other tiers
            for (size_t tr = (int)selected_tier + 1; tr < storage_tiers && qm->plan.used < QUERY_PLANS_MAX ; tr++) {
                if(!query_metric_is_valid_tier(qm, tr))
                    continue;

                // find the first time of this tier
                time_t tier_first_time_s = qm->tiers[tr].db_first_time_s;

                // can it help?
                if (tier_first_time_s < selected_tier_first_time_s) {
                    // it can help us add detail at the beginning of the query
                    QUERY_PLAN_ENTRY t = {
                        .tier = tr,
                        .after = (tier_first_time_s < after_wanted) ? after_wanted : tier_first_time_s,
                        .before = selected_tier_first_time_s,
                    };
                    ops->plans[qm->plan.used].initialized = false;
                    ops->plans[qm->plan.used].finalized = false;
                    qm->plan.array[qm->plan.used++] = t;

                    internal_fatal(!t.after || !t.before, "QUERY: invalid plan selected");

                    // prepare for the tier
                    selected_tier_first_time_s = t.after;

                    if (t.after <= after_wanted)
                        break;
                }
            }
        }

        // check if our selected tier can finish the query
        if (selected_tier_last_time_s < before_wanted) {
            // we need some help from other tiers
            for (int tr = (int)selected_tier - 1; tr >= 0 && qm->plan.used < QUERY_PLANS_MAX ; tr--) {
                if(!query_metric_is_valid_tier(qm, tr))
                    continue;

                // find the last time of this tier
                time_t tier_last_time_s = qm->tiers[tr].db_last_time_s;

                //buffer_sprintf(wb, ": EVAL BEFORE tier %d, %ld", tier, last_time_s);

                // can it help?
                if (tier_last_time_s > selected_tier_last_time_s) {
                    // it can help us add detail at the end of the query
                    QUERY_PLAN_ENTRY t = {
                        .tier = tr,
                        .after = selected_tier_last_time_s,
                        .before = (tier_last_time_s > before_wanted) ? before_wanted : tier_last_time_s,
                    };
                    ops->plans[qm->plan.used].initialized = false;
                    ops->plans[qm->plan.used].finalized = false;
                    qm->plan.array[qm->plan.used++] = t;

                    // prepare for the tier
                    selected_tier_last_time_s = t.before;

                    internal_fatal(!t.after || !t.before, "QUERY: invalid plan selected");

                    if (t.before >= before_wanted)
                        break;
                }
            }
        }
    }

    // sort the query plan
    if(qm->plan.used > 1)
        qsort(&qm->plan.array, qm->plan.used, sizeof(QUERY_PLAN_ENTRY), compare_query_plan_entries_on_start_time);

    if(!query_metric_is_valid_tier(qm, qm->plan.array[0].tier))
        return false;

#ifdef NETDATA_INTERNAL_CHECKS
    for(size_t p = 0; p < qm->plan.used ;p++) {
        internal_fatal(qm->plan.array[p].after > qm->plan.array[p].before, "QUERY: flipped after/before");
        internal_fatal(qm->plan.array[p].after < after_wanted, "QUERY: too small plan first time");
        internal_fatal(qm->plan.array[p].before > before_wanted, "QUERY: too big plan last time");
    }
#endif

    query_planer_initialize_plans(ops);
    query_planer_activate_plan(ops, 0, 0);

    return true;
}


// ----------------------------------------------------------------------------
// dimension level query engine

#define query_interpolate_point(this_point, last_point, now)      do {  \
    if(likely(                                                          \
            /* the point to interpolate is more than 1s wide */         \
            (this_point).end_time - (this_point).start_time > 1         \
                                                                        \
            /* the two points are exactly next to each other */         \
         && (last_point).end_time == (this_point).start_time            \
                                                                        \
            /* both points are valid numbers */                         \
         && netdata_double_isnumber((this_point).value)                 \
         && netdata_double_isnumber((last_point).value)                 \
                                                                        \
        )) {                                                            \
            (this_point).value = (last_point).value + ((this_point).value - (last_point).value) * (1.0 - (NETDATA_DOUBLE)((this_point).end_time - (now)) / (NETDATA_DOUBLE)((this_point).end_time - (this_point).start_time)); \
            (this_point).end_time = now;                                \
        }                                                               \
} while(0)

#define query_add_point_to_group(r, point, ops)                   do {  \
    if(likely(netdata_double_isnumber((point).value))) {                \
        if(likely(fpclassify((point).value) != FP_ZERO))                \
            (ops)->group_points_non_zero++;                             \
                                                                        \
        if(unlikely((point).flags & SN_FLAG_RESET))                     \
            (ops)->group_value_flags |= RRDR_VALUE_RESET;               \
                                                                        \
        (ops)->grouping_add(r, (point).value);                          \
    }                                                                   \
                                                                        \
    (ops)->group_points_added++;                                        \
    (ops)->group_anomaly_outlier_points = (point).anomaly_outlier_points; \
    (ops)->group_anomaly_all_points = (point).anomaly_all_points;       \
} while(0)

static __thread QUERY_ENGINE_OPS *released_ops = NULL;

static void rrd2rrdr_query_ops_freeall(RRDR *r __maybe_unused) {
    while(released_ops) {
        QUERY_ENGINE_OPS *ops = released_ops;
        released_ops = ops->next;

        onewayalloc_freez(r->internal.owa, ops);
    }
}

static void rrd2rrdr_query_ops_release(QUERY_ENGINE_OPS *ops) {
    if(!ops) return;

    ops->next = released_ops;
    released_ops = ops;
}

static QUERY_ENGINE_OPS *rrd2rrdr_query_ops_get(RRDR *r) {
    QUERY_ENGINE_OPS *ops;
    if(released_ops) {
        ops = released_ops;
        released_ops = ops->next;
    }
    else {
        ops = onewayalloc_mallocz(r->internal.owa, sizeof(QUERY_ENGINE_OPS));
    }

    memset(ops, 0, sizeof(*ops));
    return ops;
}

static QUERY_ENGINE_OPS *rrd2rrdr_query_ops_prep(RRDR *r, size_t query_metric_id) {
    QUERY_TARGET *qt = r->internal.qt;

    QUERY_ENGINE_OPS *ops = rrd2rrdr_query_ops_get(r);
    *ops = (QUERY_ENGINE_OPS) {
            .r = r,
            .qm = query_metric(qt, query_metric_id),
            .grouping_add = r->time_grouping.add,
            .grouping_flush = r->time_grouping.flush,
            .tier_query_fetch = r->time_grouping.tier_query_fetch,
            .view_update_every = r->view.update_every,
            .query_granularity = (time_t)(r->view.update_every / r->view.group),
            .group_value_flags = RRDR_VALUE_NOTHING,
    };

    if(!query_plan(ops, qt->window.after, qt->window.before, qt->window.points)) {
        rrd2rrdr_query_ops_release(ops);
        return NULL;
    }

    return ops;
}

static void rrd2rrdr_query_execute(RRDR *r, size_t dim_id_in_rrdr, QUERY_ENGINE_OPS *ops) {
    QUERY_TARGET *qt = r->internal.qt;
    QUERY_METRIC *qm = ops->qm;

    size_t points_wanted = qt->window.points;
    time_t after_wanted = qt->window.after;
    time_t before_wanted = qt->window.before; (void)before_wanted;

//    bool debug_this = false;
//    if(strcmp("user", string2str(rd->id)) == 0 && strcmp("system.cpu", string2str(rd->rrdset->id)) == 0)
//        debug_this = true;

    time_t max_date = 0,
           min_date = 0;

    size_t points_added = 0;

    long rrdr_line = -1;
    bool use_anomaly_bit_as_value = (r->view.options & RRDR_OPTION_ANOMALY_BIT) ? true : false;

    NETDATA_DOUBLE min = r->view.min, max = r->view.max;

    QUERY_POINT last2_point = QUERY_POINT_EMPTY;
    QUERY_POINT last1_point = QUERY_POINT_EMPTY;
    QUERY_POINT new_point   = QUERY_POINT_EMPTY;

    // ONE POINT READ-AHEAD
    // when we switch plans, we read-ahead a point from the next plan
    // to join them smoothly at the exact time the next plan begins
    STORAGE_POINT next1_point = STORAGE_POINT_UNSET;

    time_t now_start_time = after_wanted - ops->query_granularity;
    time_t now_end_time   = after_wanted + ops->view_update_every - ops->query_granularity;

    size_t db_points_read_since_plan_switch = 0; (void)db_points_read_since_plan_switch;
    size_t query_is_finished_counter = 0;

    // The main loop, based on the query granularity we need
    for( ; points_added < points_wanted && query_is_finished_counter <= 10 ; now_start_time = now_end_time, now_end_time += ops->view_update_every) {

        if(unlikely(query_plan_should_switch_plan(ops, now_end_time))) {
            query_planer_next_plan(ops, now_end_time, new_point.end_time);
            db_points_read_since_plan_switch = 0;
        }

        // read all the points of the db, prior to the time we need (now_end_time)

        size_t count_same_end_time = 0;
        while(count_same_end_time < 100) {
            if(likely(count_same_end_time == 0)) {
                last2_point = last1_point;
                last1_point = new_point;
            }

            if(unlikely(ops->is_finished(ops->handle))) {
                query_is_finished_counter++;

                if(count_same_end_time != 0) {
                    last2_point = last1_point;
                    last1_point = new_point;
                }
                new_point = QUERY_POINT_EMPTY;
                new_point.start_time = last1_point.end_time;
                new_point.end_time   = now_end_time;
//
//                if(debug_this) info("QUERY: is finished() returned true");
//
                break;
            }
            else
                query_is_finished_counter = 0;

            // fetch the new point
            {
                STORAGE_POINT sp;
                if(likely(storage_point_is_unset(next1_point))) {
                    db_points_read_since_plan_switch++;
                    sp = ops->next_metric(ops->handle);
                }
                else {
                    // ONE POINT READ-AHEAD
                    sp = next1_point;
                    storage_point_unset(next1_point);
                    db_points_read_since_plan_switch = 1;
                }

                // ONE POINT READ-AHEAD
                if(unlikely(query_plan_should_switch_plan(ops, sp.end_time_s) &&
                    query_planer_next_plan(ops, now_end_time, new_point.end_time))) {

                    // The end time of the current point, crosses our plans (tiers)
                    // so, we switched plan (tier)
                    //
                    // There are 2 cases now:
                    //
                    // A. the entire point of the previous plan is to the future of point from the next plan
                    // B. part of the point of the previous plan overlaps with the point from the next plan

                    STORAGE_POINT sp2 = ops->next_metric(ops->handle);

                    if(sp.start_time_s > sp2.start_time_s)
                        // the point from the previous plan is useless
                        sp = sp2;
                    else
                        // let the query run from the previous plan
                        // but setting this will also cut off the interpolation
                        // of the point from the previous plan
                        next1_point = sp2;
                }

                ops->db_points_read_per_tier[ops->tier]++;
                ops->db_total_points_read++;

                new_point.start_time = sp.start_time_s;
                new_point.end_time   = sp.end_time_s;
                new_point.anomaly_outlier_points = sp.anomaly_count;
                new_point.anomaly_all_points = sp.count;
                query_point_set_id(new_point, ops->db_total_points_read);

//                if(debug_this)
//                    info("QUERY: got point %zu, from time %ld to %ld   //   now from %ld to %ld   //   query from %ld to %ld",
//                         new_point.id, new_point.start_time, new_point.end_time, now_start_time, now_end_time, after_wanted, before_wanted);
//
                // get the right value from the point we got
                if(likely(!storage_point_is_unset(sp) && !storage_point_is_gap(sp))) {

                    if(unlikely(use_anomaly_bit_as_value))
                        new_point.value = new_point.anomaly_all_points ? (NETDATA_DOUBLE)new_point.anomaly_outlier_points * 100.0 / (NETDATA_DOUBLE)new_point.anomaly_all_points : 0.0;

                    else {
                        switch (ops->tier_query_fetch) {
                            default:
                            case TIER_QUERY_FETCH_AVERAGE:
                                new_point.value = sp.sum / (NETDATA_DOUBLE)sp.count;
                                break;

                            case TIER_QUERY_FETCH_MIN:
                                new_point.value = sp.min;
                                break;

                            case TIER_QUERY_FETCH_MAX:
                                new_point.value = sp.max;
                                break;

                            case TIER_QUERY_FETCH_SUM:
                                new_point.value = sp.sum;
                                break;
                        };
                    }
                }
                else {
                    new_point.value      = NAN;
                    new_point.flags      = SN_FLAG_NONE;
                }
            }

            // check if the db is giving us zero duration points
            if(unlikely(db_points_read_since_plan_switch > 1 &&
                        new_point.start_time == new_point.end_time)) {

                internal_error(true, "QUERY: '%s', dimension '%s' next_metric() returned "
                                     "point %zu from %ld to %ld, that are both equal",
                                     qt->id, query_metric_id(qt, qm),
                                     new_point.id, new_point.start_time, new_point.end_time);

                new_point.start_time = new_point.end_time - ops->tier_ptr->db_update_every_s;
            }

            // check if the db is advancing the query
            if(unlikely(db_points_read_since_plan_switch > 1 &&
                        new_point.end_time <= last1_point.end_time)) {

                internal_error(true,
                               "QUERY: '%s', dimension '%s' next_metric() returned "
                               "point %zu from %ld to %ld, before the "
                               "last point %zu from %ld to %ld, "
                               "now is %ld to %ld",
                               qt->id, query_metric_id(qt, qm),
                               new_point.id, new_point.start_time, new_point.end_time,
                               last1_point.id, last1_point.start_time, last1_point.end_time,
                               now_start_time, now_end_time);

                count_same_end_time++;
                continue;
            }
            count_same_end_time = 0;

            // decide how to use this point
            if(likely(new_point.end_time < now_end_time)) { // likely to favor tier0
                // this db point ends before our now_end_time

                if(likely(new_point.end_time >= now_start_time)) { // likely to favor tier0
                    // this db point ends after our now_start time

                    query_add_point_to_group(r, new_point, ops);
                }
                else {
                    // we don't need this db point
                    // it is totally outside our current time-frame

                    // this is desirable for the first point of the query
                    // because it allows us to interpolate the next point
                    // at exactly the time we will want

                    // we only log if this is not point 1
                    internal_error(new_point.end_time < ops->plan_expanded_after &&
                                   db_points_read_since_plan_switch > 1,
                                   "QUERY: '%s', dimension '%s' next_metric() "
                                   "returned point %zu from %ld time %ld, "
                                   "which is entirely before our current timeframe %ld to %ld "
                                   "(and before the entire query, after %ld, before %ld)",
                                   qt->id, query_metric_id(qt, qm),
                                   new_point.id, new_point.start_time, new_point.end_time,
                                   now_start_time, now_end_time,
                                   ops->plan_expanded_after, ops->plan_expanded_before);
                }

            }
            else {
                // the point ends in the future
                // so, we will interpolate it below, at the inner loop
                break;
            }
        }

        if(unlikely(count_same_end_time)) {
            internal_error(true,
                           "QUERY: '%s', dimension '%s', the database does not advance the query,"
                           " it returned an end time less or equal to the end time of the last "
                           "point we got %ld, %zu times",
                           qt->id, query_metric_id(qt, qm),
                           last1_point.end_time, count_same_end_time);

            if(unlikely(new_point.end_time <= last1_point.end_time))
                new_point.end_time = now_end_time;
        }

        time_t stop_time = new_point.end_time;
        if(unlikely(!storage_point_is_unset(next1_point))) {
            // ONE POINT READ-AHEAD
            // the point crosses the start time of the
            // read ahead storage point we have read
            stop_time = next1_point.start_time_s;
        }

        // the inner loop
        // we have 3 points in memory: last2, last1, new
        // we select the one to use based on their timestamps

        size_t iterations = 0;
        for ( ; now_end_time <= stop_time && points_added < points_wanted ;
                now_end_time += ops->view_update_every, iterations++) {

            // now_start_time is wrong in this loop
            // but, we don't need it

            QUERY_POINT current_point;

            if(likely(now_end_time > new_point.start_time)) {
                // it is time for our NEW point to be used
                current_point = new_point;
                query_interpolate_point(current_point, last1_point, now_end_time);

//                internal_error(current_point.id > 0
//                                && last1_point.id == 0
//                                && current_point.end_time > after_wanted
//                                && current_point.end_time > now_end_time,
//                               "QUERY: '%s', dimension '%s', after %ld, before %ld, view update every %ld,"
//                               " query granularity %ld, interpolating point %zu (from %ld to %ld) at %ld,"
//                               " but we could really favor by having last_point1 in this query.",
//                               qt->id, string2str(qm->dimension.id),
//                               after_wanted, before_wanted,
//                               ops.view_update_every, ops.query_granularity,
//                               current_point.id, current_point.start_time, current_point.end_time,
//                               now_end_time);
            }
            else if(likely(now_end_time <= last1_point.end_time)) {
                // our LAST point is still valid
                current_point = last1_point;
                query_interpolate_point(current_point, last2_point, now_end_time);

//                internal_error(current_point.id > 0
//                                && last2_point.id == 0
//                                && current_point.end_time > after_wanted
//                                && current_point.end_time > now_end_time,
//                               "QUERY: '%s', dimension '%s', after %ld, before %ld, view update every %ld,"
//                               " query granularity %ld, interpolating point %zu (from %ld to %ld) at %ld,"
//                               " but we could really favor by having last_point2 in this query.",
//                               qt->id, string2str(qm->dimension.id),
//                               after_wanted, before_wanted, ops.view_update_every, ops.query_granularity,
//                               current_point.id, current_point.start_time, current_point.end_time,
//                               now_end_time);
            }
            else {
                // a GAP, we don't have a value this time
                current_point = QUERY_POINT_EMPTY;
            }

            query_add_point_to_group(r, current_point, ops);

            rrdr_line = rrdr_line_init(r, now_end_time, rrdr_line);
            size_t rrdr_o_v_index = rrdr_line * r->d + dim_id_in_rrdr;

            if(unlikely(!min_date)) min_date = now_end_time;
            max_date = now_end_time;

            // find the place to store our values
            RRDR_VALUE_FLAGS *rrdr_value_options_ptr = &r->o[rrdr_o_v_index];

            // update the dimension options
            if(likely(ops->group_points_non_zero))
                r->od[dim_id_in_rrdr] |= RRDR_DIMENSION_NONZERO;

            // store the specific point options
            *rrdr_value_options_ptr = ops->group_value_flags;

            // store the group value
            NETDATA_DOUBLE group_value = ops->grouping_flush(r, rrdr_value_options_ptr);
            r->v[rrdr_o_v_index] = group_value;

            NETDATA_DOUBLE group_ar = r->ar[rrdr_o_v_index] = ops->group_anomaly_all_points ?
                    (NETDATA_DOUBLE)ops->group_anomaly_outlier_points * 100.0 / (NETDATA_DOUBLE)ops->group_anomaly_all_points
                    : 0.0;

            if(likely(points_added || r->internal.queries_count)) {
                // find the min/max across all dimensions

                if(unlikely(group_value < min)) min = group_value;
                if(unlikely(group_value > max)) max = group_value;

            }
            else {
                // runs only when r->internal.queries_count == 0 && points_added == 0
                // so, on the first point added for the query.
                min = max = group_value;
            }

            NETDATA_DOUBLE stats_value = group_value < 0 ? -group_value : group_value;

            if(unlikely(!points_added)) {
                qm->query_stats.min = stats_value;
                qm->query_stats.max = stats_value;
            }
            else {
                if(stats_value < qm->query_stats.min)
                    qm->query_stats.min = stats_value;

                if(stats_value > qm->query_stats.max)
                    qm->query_stats.max = stats_value;
            }

            qm->query_stats.anomaly_sum += group_ar;
            qm->query_stats.sum += stats_value;
            qm->query_stats.volume += stats_value * (NETDATA_DOUBLE)ops->view_update_every;
            qm->query_stats.group_points++;

            points_added++;
            ops->group_points_added = 0;
            ops->group_value_flags = RRDR_VALUE_NOTHING;
            ops->group_points_non_zero = 0;
            ops->group_anomaly_outlier_points = 0;
            ops->group_anomaly_all_points = 0;
        }
        // the loop above increased "now" by query_granularity,
        // but the main loop will increase it too,
        // so, let's undo the last iteration of this loop
        if(iterations)
            now_end_time   -= ops->view_update_every;
    }
    query_planer_finalize_remaining_plans(ops);

    // fill the rest of the points with empty values
    while (points_added < points_wanted) {
        if(!max_date)
            min_date = max_date = after_wanted + ops->view_update_every - ops->query_granularity;
        else
            max_date += ops->view_update_every;

        rrdr_line = rrdr_line_init(r, max_date, rrdr_line);
        size_t rrdr_o_v_index = rrdr_line * r->d + dim_id_in_rrdr;
        r->o[rrdr_o_v_index] = RRDR_VALUE_EMPTY;
        r->v[rrdr_o_v_index] = 0.0;
        r->ar[rrdr_o_v_index] = 0.0;

        points_added++;
    }

    r->internal.queries_count++;
    r->view.min = min;
    r->view.max = max;
    r->view.before = max_date;
    r->view.after = min_date - ops->view_update_every + ops->query_granularity;
    r->rows = rrdr_line + 1;

    r->stats.result_points_generated += points_added;
    r->stats.db_points_read += ops->db_total_points_read;
    for(size_t tr = 0; tr < storage_tiers ; tr++)
        qt->db.tiers[tr].points += ops->db_points_read_per_tier[tr];

    internal_error(points_added != points_wanted,
                   "QUERY: '%s', dimension '%s', requested %zu points, but RRDR added %zu (%zu db points read).",
                   qt->id, query_metric_id(qt, qm),
                   (size_t)points_wanted, (size_t)points_added, ops->db_total_points_read);
}

// ----------------------------------------------------------------------------
// fill the gap of a tier

void store_metric_at_tier(RRDDIM *rd, size_t tier, struct rrddim_tier *t, STORAGE_POINT sp, usec_t now_ut);

void rrdr_fill_tier_gap_from_smaller_tiers(RRDDIM *rd, size_t tier, time_t now_s) {
    if(unlikely(tier >= storage_tiers)) return;
    if(storage_tiers_backfill[tier] == RRD_BACKFILL_NONE) return;

    struct rrddim_tier *t = &rd->tiers[tier];
    if(unlikely(!t)) return;

    time_t latest_time_s = t->query_ops->latest_time_s(t->db_metric_handle);
    time_t granularity = (time_t)t->tier_grouping * (time_t)rd->update_every;
    time_t time_diff   = now_s - latest_time_s;

    // if the user wants only NEW backfilling, and we don't have any data
    if(storage_tiers_backfill[tier] == RRD_BACKFILL_NEW && latest_time_s <= 0) return;

    // there is really nothing we can do
    if(now_s <= latest_time_s || time_diff < granularity) return;

    struct storage_engine_query_handle handle;

    // for each lower tier
    for(int read_tier = (int)tier - 1; read_tier >= 0 ; read_tier--){
        time_t smaller_tier_first_time = rd->tiers[read_tier].query_ops->oldest_time_s(rd->tiers[read_tier].db_metric_handle);
        time_t smaller_tier_last_time = rd->tiers[read_tier].query_ops->latest_time_s(rd->tiers[read_tier].db_metric_handle);
        if(smaller_tier_last_time <= latest_time_s) continue;  // it is as bad as we are

        long after_wanted = (latest_time_s < smaller_tier_first_time) ? smaller_tier_first_time : latest_time_s;
        long before_wanted = smaller_tier_last_time;

        struct rrddim_tier *tmp = &rd->tiers[read_tier];
        tmp->query_ops->init(tmp->db_metric_handle, &handle, after_wanted, before_wanted, STORAGE_PRIORITY_HIGH);

        size_t points_read = 0;

        while(!tmp->query_ops->is_finished(&handle)) {

            STORAGE_POINT sp = tmp->query_ops->next_metric(&handle);
            points_read++;

            if(sp.end_time_s > latest_time_s) {
                latest_time_s = sp.end_time_s;
                store_metric_at_tier(rd, tier, t, sp, sp.end_time_s * USEC_PER_SEC);
            }
        }

        tmp->query_ops->finalize(&handle);
        store_metric_collection_completed();
        global_statistics_backfill_query_completed(points_read);

        //internal_error(true, "DBENGINE: backfilled chart '%s', dimension '%s', tier %d, from %ld to %ld, with %zu points from tier %d",
        //               rd->rrdset->name, rd->name, tier, after_wanted, before_wanted, points, tr);
    }
}

// ----------------------------------------------------------------------------
// fill RRDR for the whole chart

#ifdef NETDATA_INTERNAL_CHECKS
static void rrd2rrdr_log_request_response_metadata(RRDR *r
        , RRDR_OPTIONS options __maybe_unused
        , RRDR_TIME_GROUPING group_method
        , bool aligned
        , size_t group
        , time_t resampling_time
        , size_t resampling_group
        , time_t after_wanted
        , time_t after_requested
        , time_t before_wanted
        , time_t before_requested
        , size_t points_requested
        , size_t points_wanted
        //, size_t after_slot
        //, size_t before_slot
        , const char *msg
        ) {

    QUERY_TARGET *qt = r->internal.qt;
    time_t first_entry_s = qt->db.first_time_s;
    time_t last_entry_s = qt->db.last_time_s;

    internal_error(
    true,
    "rrd2rrdr() on %s update every %ld with %s grouping %s (group: %zu, resampling_time: %ld, resampling_group: %zu), "
         "after (got: %ld, want: %ld, req: %ld, db: %ld), "
         "before (got: %ld, want: %ld, req: %ld, db: %ld), "
         "duration (got: %ld, want: %ld, req: %ld, db: %ld), "
         "points (got: %zu, want: %zu, req: %zu), "
         "%s"
         , qt->id
         , qt->window.query_granularity

         // grouping
         , (aligned) ? "aligned" : "unaligned"
         , time_grouping_method2string(group_method)
         , group
         , resampling_time
         , resampling_group

         // after
         , r->view.after
         , after_wanted
         , after_requested
         , first_entry_s

         // before
         , r->view.before
         , before_wanted
         , before_requested
         , last_entry_s

         // duration
         , (long)(r->view.before - r->view.after + qt->window.query_granularity)
         , (long)(before_wanted - after_wanted + qt->window.query_granularity)
         , (long)before_requested - after_requested
         , (long)((last_entry_s - first_entry_s) + qt->window.query_granularity)

         // points
         , r->rows
         , points_wanted
         , points_requested

         // message
         , msg
    );
}
#endif // NETDATA_INTERNAL_CHECKS

// Returns 1 if an absolute period was requested or 0 if it was a relative period
bool rrdr_relative_window_to_absolute(time_t *after, time_t *before, time_t *now_ptr) {
    time_t now = now_realtime_sec() - 1;

    if(now_ptr)
        *now_ptr = now;

    int absolute_period_requested = -1;
    long long after_requested, before_requested;

    before_requested = *before;
    after_requested = *after;

    // allow relative for before (smaller than API_RELATIVE_TIME_MAX)
    if(ABS(before_requested) <= API_RELATIVE_TIME_MAX) {
        // if the user asked for a positive relative time,
        // flip it to a negative
        if(before_requested > 0)
            before_requested = -before_requested;

        before_requested = now + before_requested;
        absolute_period_requested = 0;
    }

    // allow relative for after (smaller than API_RELATIVE_TIME_MAX)
    if(ABS(after_requested) <= API_RELATIVE_TIME_MAX) {
        if(after_requested > 0)
            after_requested = -after_requested;

        // if the user didn't give an after, use the number of points
        // to give a sane default
        if(after_requested == 0)
            after_requested = -600;

        // since the query engine now returns inclusive timestamps
        // it is awkward to return 6 points when after=-5 is given
        // so for relative queries we add 1 second, to give
        // more predictable results to users.
        after_requested = before_requested + after_requested + 1;
        absolute_period_requested = 0;
    }

    if(absolute_period_requested == -1)
        absolute_period_requested = 1;

    // check if the parameters are flipped
    if(after_requested > before_requested) {
        long long t = before_requested;
        before_requested = after_requested;
        after_requested = t;
    }

    // if the query requests future data
    // shift the query back to be in the present time
    // (this may also happen because of the rules above)
    if(before_requested > now) {
        long long delta = before_requested - now;
        before_requested -= delta;
        after_requested  -= delta;
    }

    time_t absolute_minimum_time = now - (10 * 365 * 86400);
    time_t absolute_maximum_time = now + (1 * 365 * 86400);

    if (after_requested < absolute_minimum_time && !unittest_running)
        after_requested = absolute_minimum_time;

    if (after_requested > absolute_maximum_time && !unittest_running)
        after_requested = absolute_maximum_time;

    if (before_requested < absolute_minimum_time && !unittest_running)
        before_requested = absolute_minimum_time;

    if (before_requested > absolute_maximum_time && !unittest_running)
        before_requested = absolute_maximum_time;

    *before = before_requested;
    *after = after_requested;

    return (absolute_period_requested != 1);
}

// #define DEBUG_QUERY_LOGIC 1

#ifdef DEBUG_QUERY_LOGIC
#define query_debug_log_init() BUFFER *debug_log = buffer_create(1000)
#define query_debug_log(args...) buffer_sprintf(debug_log, ##args)
#define query_debug_log_fin() { \
        info("QUERY: '%s', after:%ld, before:%ld, duration:%ld, points:%zu, res:%ld - wanted => after:%ld, before:%ld, points:%zu, group:%zu, granularity:%ld, resgroup:%ld, resdiv:" NETDATA_DOUBLE_FORMAT_AUTO " %s", qt->id, after_requested, before_requested, before_requested - after_requested, points_requested, resampling_time_requested, after_wanted, before_wanted, points_wanted, group, query_granularity, resampling_group, resampling_divisor, buffer_tostring(debug_log)); \
        buffer_free(debug_log); \
        debug_log = NULL; \
    }
#define query_debug_log_free() do { buffer_free(debug_log); } while(0)
#else
#define query_debug_log_init() debug_dummy()
#define query_debug_log(args...) debug_dummy()
#define query_debug_log_fin() debug_dummy()
#define query_debug_log_free() debug_dummy()
#endif

bool query_target_calculate_window(QUERY_TARGET *qt) {
    if (unlikely(!qt)) return false;

    size_t points_requested = (long)qt->request.points;
    time_t after_requested = qt->request.after;
    time_t before_requested = qt->request.before;
    RRDR_TIME_GROUPING group_method = qt->request.time_group_method;
    time_t resampling_time_requested = qt->request.resampling_time;
    RRDR_OPTIONS options = qt->request.options;
    size_t tier = qt->request.tier;
    time_t update_every = qt->db.minimum_latest_update_every_s;

    // RULES
    // points_requested = 0
    // the user wants all the natural points the database has
    //
    // after_requested = 0
    // the user wants to start the query from the oldest point in our database
    //
    // before_requested = 0
    // the user wants the query to end to the latest point in our database
    //
    // when natural points are wanted, the query has to be aligned to the update_every
    // of the database

    size_t points_wanted = points_requested;
    time_t after_wanted = after_requested;
    time_t before_wanted = before_requested;

    bool aligned = !(options & RRDR_OPTION_NOT_ALIGNED);
    bool automatic_natural_points = (points_wanted == 0);
    bool relative_period_requested = false;
    bool natural_points = (options & RRDR_OPTION_NATURAL_POINTS) || automatic_natural_points;
    bool before_is_aligned_to_db_end = false;

    query_debug_log_init();

    if (ABS(before_requested) <= API_RELATIVE_TIME_MAX || ABS(after_requested) <= API_RELATIVE_TIME_MAX) {
        relative_period_requested = true;
        natural_points = true;
        options |= RRDR_OPTION_NATURAL_POINTS;
        query_debug_log(":relative+natural");
    }

    // if the user wants virtual points, make sure we do it
    if (options & RRDR_OPTION_VIRTUAL_POINTS)
        natural_points = false;

    // set the right flag about natural and virtual points
    if (natural_points) {
        options |= RRDR_OPTION_NATURAL_POINTS;

        if (options & RRDR_OPTION_VIRTUAL_POINTS)
            options &= ~RRDR_OPTION_VIRTUAL_POINTS;
    }
    else {
        options |= RRDR_OPTION_VIRTUAL_POINTS;

        if (options & RRDR_OPTION_NATURAL_POINTS)
            options &= ~RRDR_OPTION_NATURAL_POINTS;
    }

    if (after_wanted == 0 || before_wanted == 0) {
        relative_period_requested = true;

        time_t first_entry_s = qt->db.first_time_s;
        time_t last_entry_s = qt->db.last_time_s;

        if (first_entry_s == 0 || last_entry_s == 0) {
            internal_error(true, "QUERY: no data detected on query '%s' (db first_entry_t = %ld, last_entry_t = %ld", qt->id, first_entry_s, last_entry_s);
            query_debug_log_free();
            return false;
        }

        query_debug_log(":first_entry_t %ld, last_entry_t %ld", first_entry_s, last_entry_s);

        if (after_wanted == 0) {
            after_wanted = first_entry_s;
            query_debug_log(":zero after_wanted %ld", after_wanted);
        }

        if (before_wanted == 0) {
            before_wanted = last_entry_s;
            before_is_aligned_to_db_end = true;
            query_debug_log(":zero before_wanted %ld", before_wanted);
        }

        if (points_wanted == 0) {
            points_wanted = (last_entry_s - first_entry_s) / update_every;
            query_debug_log(":zero points_wanted %zu", points_wanted);
        }
    }

    if (points_wanted == 0) {
        points_wanted = 600;
        query_debug_log(":zero600 points_wanted %zu", points_wanted);
    }

    // convert our before_wanted and after_wanted to absolute
    rrdr_relative_window_to_absolute(&after_wanted, &before_wanted, NULL);
    query_debug_log(":relative2absolute after %ld, before %ld", after_wanted, before_wanted);

    if (natural_points && (options & RRDR_OPTION_SELECTED_TIER) && tier > 0 && storage_tiers > 1) {
        update_every = rrdset_find_natural_update_every_for_timeframe(
                qt, after_wanted, before_wanted, points_wanted, options, tier);

        if (update_every <= 0) update_every = qt->db.minimum_latest_update_every_s;
        query_debug_log(":natural update every %ld", update_every);
    }

    // this is the update_every of the query
    // it may be different to the update_every of the database
    time_t query_granularity = (natural_points) ? update_every : 1;
    if (query_granularity <= 0) query_granularity = 1;
    query_debug_log(":query_granularity %ld", query_granularity);

    // align before_wanted and after_wanted to query_granularity
    if (before_wanted % query_granularity) {
        before_wanted -= before_wanted % query_granularity;
        query_debug_log(":granularity align before_wanted %ld", before_wanted);
    }

    if (after_wanted % query_granularity) {
        after_wanted -= after_wanted % query_granularity;
        query_debug_log(":granularity align after_wanted %ld", after_wanted);
    }

    // automatic_natural_points is set when the user wants all the points available in the database
    if (automatic_natural_points) {
        points_wanted = (before_wanted - after_wanted + 1) / query_granularity;
        if (unlikely(points_wanted <= 0)) points_wanted = 1;
        query_debug_log(":auto natural points_wanted %zu", points_wanted);
    }

    time_t duration = before_wanted - after_wanted;

    // if the resampling time is too big, extend the duration to the past
    if (unlikely(resampling_time_requested > duration)) {
        after_wanted = before_wanted - resampling_time_requested;
        duration = before_wanted - after_wanted;
        query_debug_log(":resampling after_wanted %ld", after_wanted);
    }

    // if the duration is not aligned to resampling time
    // extend the duration to the past, to avoid a gap at the chart
    // only when the missing duration is above 1/10th of a point
    if (resampling_time_requested > query_granularity && duration % resampling_time_requested) {
        time_t delta = duration % resampling_time_requested;
        if (delta > resampling_time_requested / 10) {
            after_wanted -= resampling_time_requested - delta;
            duration = before_wanted - after_wanted;
            query_debug_log(":resampling2 after_wanted %ld", after_wanted);
        }
    }

    // the available points of the query
    size_t points_available = (duration + 1) / query_granularity;
    if (unlikely(points_available <= 0)) points_available = 1;
    query_debug_log(":points_available %zu", points_available);

    if (points_wanted > points_available) {
        points_wanted = points_available;
        query_debug_log(":max points_wanted %zu", points_wanted);
    }

    if(points_wanted > 86400 && !unittest_running) {
        points_wanted = 86400;
        query_debug_log(":absolute max points_wanted %zu", points_wanted);
    }

    // calculate the desired grouping of source data points
    size_t group = points_available / points_wanted;
    if (group == 0) group = 1;

    // round "group" to the closest integer
    if (points_available % points_wanted > points_wanted / 2)
        group++;

    query_debug_log(":group %zu", group);

    if (points_wanted * group * query_granularity < (size_t)duration) {
        // the grouping we are going to do, is not enough
        // to cover the entire duration requested, so
        // we have to change the number of points, to make sure we will
        // respect the timeframe as closely as possibly

        // let's see how many points are the optimal
        points_wanted = points_available / group;

        if (points_wanted * group < points_available)
            points_wanted++;

        if (unlikely(points_wanted == 0))
            points_wanted = 1;

        query_debug_log(":optimal points %zu", points_wanted);
    }

    // resampling_time_requested enforces a certain grouping multiple
    NETDATA_DOUBLE resampling_divisor = 1.0;
    size_t resampling_group = 1;
    if (unlikely(resampling_time_requested > query_granularity)) {
        // the points we should group to satisfy gtime
        resampling_group = resampling_time_requested / query_granularity;
        if (unlikely(resampling_time_requested % query_granularity))
            resampling_group++;

        query_debug_log(":resampling group %zu", resampling_group);

        // adapt group according to resampling_group
        if (unlikely(group < resampling_group)) {
            group = resampling_group; // do not allow grouping below the desired one
            query_debug_log(":group less res %zu", group);
        }
        if (unlikely(group % resampling_group)) {
            group += resampling_group - (group % resampling_group); // make sure group is multiple of resampling_group
            query_debug_log(":group mod res %zu", group);
        }

        // resampling_divisor = group / resampling_group;
        resampling_divisor = (NETDATA_DOUBLE) (group * query_granularity) / (NETDATA_DOUBLE) resampling_time_requested;
        query_debug_log(":resampling divisor " NETDATA_DOUBLE_FORMAT, resampling_divisor);
    }

    // now that we have group, align the requested timeframe to fit it.
    if (aligned && before_wanted % (group * query_granularity)) {
        if (before_is_aligned_to_db_end)
            before_wanted -= before_wanted % (time_t)(group * query_granularity);
        else
            before_wanted += (time_t)(group * query_granularity) - before_wanted % (time_t)(group * query_granularity);
        query_debug_log(":align before_wanted %ld", before_wanted);
    }

    after_wanted = before_wanted - (time_t)(points_wanted * group * query_granularity) + query_granularity;
    query_debug_log(":final after_wanted %ld", after_wanted);

    duration = before_wanted - after_wanted;
    query_debug_log(":final duration %ld", duration + 1);

    query_debug_log_fin();

    internal_error(points_wanted != duration / (query_granularity * group) + 1,
                   "QUERY: points_wanted %zu is not points %zu",
                   points_wanted, (size_t)(duration / (query_granularity * group) + 1));

    internal_error(group < resampling_group,
                   "QUERY: group %zu is less than the desired group points %zu",
                   group, resampling_group);

    internal_error(group > resampling_group && group % resampling_group,
                   "QUERY: group %zu is not a multiple of the desired group points %zu",
                   group, resampling_group);

    // -------------------------------------------------------------------------
    // update QUERY_TARGET with our calculations

    qt->window.after = after_wanted;
    qt->window.before = before_wanted;
    qt->window.relative = relative_period_requested;
    qt->window.points = points_wanted;
    qt->window.group = group;
    qt->window.group_method = group_method;
    qt->window.group_options = qt->request.time_group_options;
    qt->window.query_granularity = query_granularity;
    qt->window.resampling_group = resampling_group;
    qt->window.resampling_divisor = resampling_divisor;
    qt->window.options = options;
    qt->window.tier = tier;
    qt->window.aligned = aligned;

    return true;
}

void query_target_merge_data_statistics(struct query_data_statistics *d, struct query_data_statistics *s) {
    if(!d->group_points)
        *d = *s;
    else {
        d->group_points += s->group_points;
        d->sum += s->sum;
        d->anomaly_sum += s->anomaly_sum;
        d->volume += s->volume;

        if(s->min < d->min)
            d->min = s->min;

        if(s->max > d->max)
            d->max = s->max;
    }
}

// ----------------------------------------------------------------------------
// group by

struct group_by_label_key {
    DICTIONARY *values;
};

static void group_by_label_key_insert_cb(const DICTIONARY_ITEM *item __maybe_unused, void *value, void *data) {
    // add the key to our r->label_keys global keys dictionary
    DICTIONARY *label_keys = data;
    dictionary_set(label_keys, dictionary_acquired_item_name(item), NULL, 0);

    // create a dictionary for the values of this key
    struct group_by_label_key *k = value;
    k->values = dictionary_create_advanced(DICT_OPTION_SINGLE_THREADED | DICT_OPTION_DONT_OVERWRITE_VALUE, NULL, 0);
}

static void group_by_label_key_delete_cb(const DICTIONARY_ITEM *item __maybe_unused, void *value, void *data __maybe_unused) {
    struct group_by_label_key *k = value;
    dictionary_destroy(k->values);
}

static int rrdlabels_traversal_cb_to_group_by_label_key(const char *name, const char *value, RRDLABEL_SRC ls __maybe_unused, void *data) {
    DICTIONARY *dl = data;
    struct group_by_label_key *k = dictionary_set(dl, name, NULL, sizeof(struct group_by_label_key));
    dictionary_set(k->values, value, NULL, 0);
    return 1;
}

void rrdr_json_group_by_labels(BUFFER *wb, const char *key, RRDR *r, RRDR_OPTIONS options) {
    if(!r->label_keys || !r->dl)
        return;

    buffer_json_member_add_object(wb, key);

    void *t;
    dfe_start_read(r->label_keys, t) {
                buffer_json_member_add_array(wb, t_dfe.name);

                for(size_t d = 0; d < r->d ;d++) {
                    if(!rrdr_dimension_should_be_exposed(r->od[d], options))
                        continue;

                    struct group_by_label_key *k = dictionary_get(r->dl[d], t_dfe.name);
                    if(k) {
                        buffer_json_add_array_item_array(wb);
                        void *tt;
                        dfe_start_read(k->values, tt) {
                                    buffer_json_add_array_item_string(wb, tt_dfe.name);
                                }
                        dfe_done(tt);
                        buffer_json_array_close(wb);
                    }
                    else
                        buffer_json_add_array_item_string(wb, NULL);
                }

                buffer_json_array_close(wb);
            }
    dfe_done(t);

    buffer_json_object_close(wb); // key
}

static int group_by_label_is_space(char c) {
    if(c == ',' || c == '|')
        return 1;

    return 0;
}

static RRDR *rrd2rrdr_group_by_initialize(ONEWAYALLOC *owa, QUERY_TARGET *qt) {
    RRDR_OPTIONS options = qt->request.options;

    if(qt->request.group_by == RRDR_GROUP_BY_NONE) {
        RRDR *r = rrdr_create(owa, qt, qt->query.used, qt->window.points);
         if(unlikely(!r)) {
             internal_error(true, "QUERY: cannot create RRDR for %s, after=%ld, before=%ld, dimensions=%u, points=%zu",
                            qt->id, qt->window.after, qt->window.before, qt->query.used, qt->window.points);
             query_target_release(qt);
             return NULL;
         }
         r->group_by.r = NULL;

         for(size_t d = 0; d < qt->query.used ; d++) {
             QUERY_METRIC *qm = query_metric(qt, d);
             QUERY_DIMENSION *qd = query_dimension(qt, qm->link.query_dimension_id);
             r->di[d] = rrdmetric_acquired_id_dup(qd->rma);
             r->dn[d] = rrdmetric_acquired_name_dup(qd->rma);
         }

         return r;
    }

    struct rrdr_group_by_entry *entries = onewayalloc_callocz(owa, qt->query.used, sizeof(struct rrdr_group_by_entry));
    DICTIONARY *groups = dictionary_create(DICT_OPTION_SINGLE_THREADED | DICT_OPTION_DONT_OVERWRITE_VALUE);

    if(qt->request.group_by & RRDR_GROUP_BY_LABEL && qt->request.group_by_label && *qt->request.group_by_label)
        qt->group_by.used = quoted_strings_splitter(qt->request.group_by_label, qt->group_by.label_keys, GROUP_BY_MAX_LABEL_KEYS, group_by_label_is_space);

    if(!qt->group_by.used)
        qt->request.group_by &= ~RRDR_GROUP_BY_LABEL;

    if(!(qt->request.group_by & (RRDR_GROUP_BY_SELECTED | RRDR_GROUP_BY_DIMENSION | RRDR_GROUP_BY_INSTANCE | RRDR_GROUP_BY_LABEL | RRDR_GROUP_BY_NODE | RRDR_GROUP_BY_CONTEXT)))
        qt->request.group_by = RRDR_GROUP_BY_DIMENSION;

    DICTIONARY *label_keys = NULL;
    if(options & RRDR_OPTION_GROUP_BY_LABELS)
        label_keys = dictionary_create_advanced(DICT_OPTION_SINGLE_THREADED | DICT_OPTION_DONT_OVERWRITE_VALUE, NULL, 0);

    int added = 0;
    BUFFER *key = buffer_create(0, NULL);
    QUERY_INSTANCE *last_qi = NULL;
    size_t priority = 0;
    time_t update_every_max = 0;
    for(size_t d = 0; d < qt->query.used ; d++) {
        QUERY_METRIC *qm = query_metric(qt, d);
        QUERY_INSTANCE *qi = query_instance(qt, qm->link.query_instance_id);
        QUERY_CONTEXT *qc = query_context(qt, qm->link.query_context_id);
        QUERY_NODE *qn = query_node(qt, qm->link.query_node_id);

        if(qi != last_qi) {
            priority = 0;
            last_qi = qi;

            time_t update_every = rrdinstance_acquired_update_every(qi->ria);
            if(update_every > update_every_max)
                update_every_max = update_every;
        }
        else
            priority++;

        // --------------------------------------------------------------------
        // generate the group by key

        buffer_flush(key);
        if(unlikely(qm->status & RRDR_DIMENSION_HIDDEN)) {
            buffer_strcat(key, "__hidden_dimensions__");
        }
        else if(unlikely(qt->request.group_by & RRDR_GROUP_BY_SELECTED)) {
            buffer_strcat(key, "selected");
        }
        else {
            if (qt->request.group_by & RRDR_GROUP_BY_DIMENSION) {
                buffer_fast_strcat(key, "|", 1);
                buffer_strcat(key, query_metric_id(qt, qm));
            }

            if (qt->request.group_by & RRDR_GROUP_BY_INSTANCE) {
                buffer_fast_strcat(key, "|", 1);
                buffer_strcat(key, string2str(query_instance_id_fqdn(qt, qi)));
            }

            if (qt->request.group_by & RRDR_GROUP_BY_LABEL) {
                DICTIONARY *labels = rrdinstance_acquired_labels(qi->ria);
                for (size_t l = 0; l < qt->group_by.used; l++) {
                    buffer_fast_strcat(key, "|", 1);
                    rrdlabels_get_value_to_buffer_or_unset(labels, key, qt->group_by.label_keys[l], "[unset]");
                }
            }

            if (qt->request.group_by & RRDR_GROUP_BY_NODE) {
                buffer_fast_strcat(key, "|", 1);
                buffer_strcat(key, qn->rrdhost->machine_guid);
            }

            if (qt->request.group_by & RRDR_GROUP_BY_CONTEXT) {
                buffer_fast_strcat(key, "|", 1);
                buffer_strcat(key, rrdcontext_acquired_id(qc->rca));
            }

            if (qt->request.group_by & RRDR_GROUP_BY_UNITS) {
                buffer_fast_strcat(key, "|", 1);
                buffer_strcat(key, query_target_has_percentage_units(qt) ? "%" : rrdinstance_acquired_units(qi->ria));
            }
        }

        // lookup the key in the dictionary

        int pos = -1;
        int *set = dictionary_set(groups, buffer_tostring(key), &pos, sizeof(pos));
        if(*set == -1) {
            // the key just added to the dictionary

            *set = pos = added++;

            // ----------------------------------------------------------------
            // generate the dimension id

            buffer_flush(key);
            if(unlikely(qm->status & RRDR_DIMENSION_HIDDEN)) {
                buffer_strcat(key, "__hidden_dimensions__");
            }
            else if(unlikely(qt->request.group_by & RRDR_GROUP_BY_SELECTED)) {
                buffer_strcat(key, "selected");
            }
            else {
                if (qt->request.group_by & RRDR_GROUP_BY_DIMENSION) {
                    buffer_strcat(key, query_metric_id(qt, qm));
                }

                if (qt->request.group_by & RRDR_GROUP_BY_INSTANCE) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    if (qt->request.group_by & RRDR_GROUP_BY_NODE)
                        buffer_strcat(key, rrdinstance_acquired_id(qi->ria));
                    else
                        buffer_strcat(key, string2str(query_instance_id_fqdn(qt, qi)));
                }

                if (qt->request.group_by & RRDR_GROUP_BY_LABEL) {
                    DICTIONARY *labels = rrdinstance_acquired_labels(qi->ria);
                    for (size_t l = 0; l < qt->group_by.used; l++) {
                        if (buffer_strlen(key) != 0)
                            buffer_fast_strcat(key, ",", 1);
                        rrdlabels_get_value_to_buffer_or_unset(labels, key, qt->group_by.label_keys[l], "[unset]");
                    }
                }

                if (qt->request.group_by & RRDR_GROUP_BY_NODE) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    buffer_strcat(key, qn->rrdhost->machine_guid);
                }

                if (qt->request.group_by & RRDR_GROUP_BY_CONTEXT) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    buffer_strcat(key, rrdcontext_acquired_id(qc->rca));
                }

                if (qt->request.group_by & RRDR_GROUP_BY_UNITS) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    buffer_strcat(key, query_target_has_percentage_units(qt) ? "%" : rrdinstance_acquired_units(qi->ria));
                }
            }

            entries[pos].id = string_strdupz(buffer_tostring(key));

            // ----------------------------------------------------------------
            // generate the dimension name

            buffer_flush(key);
            if(unlikely(qm->status & RRDR_DIMENSION_HIDDEN)) {
                buffer_strcat(key, "__hidden_dimensions__");
            }
            else if(unlikely(qt->request.group_by & RRDR_GROUP_BY_SELECTED)) {
                buffer_strcat(key, "selected");
            }
            else {
                if (qt->request.group_by & RRDR_GROUP_BY_DIMENSION) {
                    buffer_strcat(key, query_metric_name(qt, qm));
                }

                if (qt->request.group_by & RRDR_GROUP_BY_INSTANCE) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    if (qt->request.group_by & RRDR_GROUP_BY_NODE)
                        buffer_strcat(key, rrdinstance_acquired_name(qi->ria));
                    else
                        buffer_strcat(key, string2str(query_instance_name_fqdn(qt, qi)));
                }

                if (qt->request.group_by & RRDR_GROUP_BY_LABEL) {
                    DICTIONARY *labels = rrdinstance_acquired_labels(qi->ria);
                    for (size_t l = 0; l < qt->group_by.used; l++) {
                        if (buffer_strlen(key) != 0)
                            buffer_fast_strcat(key, ",", 1);
                        rrdlabels_get_value_to_buffer_or_unset(labels, key, qt->group_by.label_keys[l], "[unset]");
                    }
                }

                if (qt->request.group_by & RRDR_GROUP_BY_NODE) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    buffer_strcat(key, rrdhost_hostname(qn->rrdhost));
                }

                if (qt->request.group_by & RRDR_GROUP_BY_CONTEXT) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    buffer_strcat(key, rrdcontext_acquired_id(qc->rca));
                }

                if (qt->request.group_by & RRDR_GROUP_BY_UNITS) {
                    if (buffer_strlen(key) != 0)
                        buffer_fast_strcat(key, ",", 1);

                    buffer_strcat(key, query_target_has_percentage_units(qt) ? "%" : rrdinstance_acquired_units(qi->ria));
                }
            }

            entries[pos].name = string_strdupz(buffer_tostring(key));

            // add the rest of the info
            entries[pos].units = rrdinstance_acquired_units_dup(qi->ria);
            entries[pos].priority = priority;

            if(options & RRDR_OPTION_GROUP_BY_LABELS) {
                entries[pos].dl = dictionary_create_advanced(
                        DICT_OPTION_SINGLE_THREADED | DICT_OPTION_FIXED_SIZE | DICT_OPTION_DONT_OVERWRITE_VALUE,
                        NULL, sizeof(struct group_by_label_key));
                dictionary_register_insert_callback(entries[pos].dl, group_by_label_key_insert_cb, label_keys);
                dictionary_register_delete_callback(entries[pos].dl, group_by_label_key_delete_cb, label_keys);
            }
        }
        else {
            // the key found in the dictionary
            pos = *set;
        }

        entries[pos].count++;

        if(unlikely(priority < entries[pos].priority))
            entries[pos].priority = priority;

        qm->grouped_as.slot = pos;
        qm->grouped_as.id = entries[pos].id;
        qm->grouped_as.name = entries[pos].name;
        qm->grouped_as.units = entries[pos].units;

        // copy the dimension flags decided by the query target
        // we need this, because if a dimension is explicitly selected
        // the query target adds to it the non-zero flag
        qm->status |= RRDR_DIMENSION_GROUPED;
        entries[pos].od |= qm->status;

        if(entries[pos].dl)
            rrdlabels_walkthrough_read(rrdinstance_acquired_labels(qi->ria),
                                       rrdlabels_traversal_cb_to_group_by_label_key, entries[pos].dl);
    }

    // check if we have multiple units
    bool multiple_units = false;
    for(int i = 1; i < added ; i++) {
        if(entries[i].units != entries[0].units) {
            multiple_units = true;
            break;
        }
    }

    if(multiple_units) {
        // include the units into the id and name of the dimensions
        for(int i = 0; i < added ; i++) {
            buffer_flush(key);
            buffer_strcat(key, string2str(entries[i].id));
            buffer_fast_strcat(key, ",", 1);
            buffer_strcat(key, string2str(entries[i].units));
            STRING *u = string_strdupz(buffer_tostring(key));
            string_freez(entries[i].id);
            entries[i].id = u;
        }
    }

    RRDR *r = rrdr_create(owa, qt, added, qt->window.points);
    if(!r) {
        internal_error(true, "QUERY: cannot create group by RRDR for %s, after=%ld, before=%ld, dimensions=%d, points=%zu",
                       qt->id, qt->window.after, qt->window.before, added, qt->window.points);
        goto cleanup;
    }

    r->group_by.r = rrdr_create(owa, qt, 1, qt->window.points);
    if(!r->group_by.r) {
        internal_error(true, "QUERY: cannot create group by temporary RRDR for %s, after=%ld, before=%ld, dimensions=%d, points=%zu",
                       qt->id, qt->window.after, qt->window.before, 1, qt->window.points);
        goto cleanup;
    }

    r->dp = onewayalloc_callocz(r->internal.owa, r->d, sizeof(*r->dp));
    r->dv = onewayalloc_callocz(r->internal.owa, r->d, sizeof(*r->dv));
    r->dgbc = onewayalloc_callocz(r->internal.owa, r->d, sizeof(*r->dgbc));
    r->gbc = onewayalloc_callocz(r->internal.owa, r->n * r->d, sizeof(*r->gbc));

    if(options & RRDR_OPTION_GROUP_BY_LABELS) {
        r->dl = onewayalloc_callocz(r->internal.owa, r->d, sizeof(DICTIONARY *));
        r->label_keys = label_keys;
    }

    // zero r (dimension options, names, and ids)
    // this is required, because group-by may lead to empty dimensions
    for(size_t d = 0; d < r->d ; d++) {
        r->di[d] = entries[d].id;
        r->dn[d] = entries[d].name;

        r->od[d] = entries[d].od;
        r->du[d] = entries[d].units;
        r->dp[d] = entries[d].priority;
        r->dgbc[d] = entries[d].count;

        if(r->dl)
            r->dl[d] = entries[d].dl;
    }

    // initialize partial trimming
    r->partial_data_trimming.max_update_every = update_every_max;
    r->partial_data_trimming.expected_after =
            (!(qt->request.options & RRDR_OPTION_RETURN_RAW) && qt->window.before >= qt->window.now - update_every_max) ?
            qt->window.before - update_every_max :
            qt->window.before;
    r->partial_data_trimming.trimmed_after = qt->window.before;

    // make all values empty
    for(size_t i = 0; i != r->n ;i++) {
        NETDATA_DOUBLE *cn = &r->v[ i * r->d ];
        RRDR_VALUE_FLAGS *co = &r->o[ i * r->d ];
        NETDATA_DOUBLE *ar = &r->ar[ i * r->d ];
        for (size_t d = 0; d < r->d; d++) {
            cn[d] = 0.0;
            ar[d] = 0.0;
            co[d] = RRDR_VALUE_EMPTY;
        }
    }

cleanup:
    buffer_free(key);

    if(!r) {
        if(entries) {
            for (int d2 = 0; d2 < added; d2++) {
                string_freez(entries[d2].id);
                string_freez(entries[d2].name);
                dictionary_destroy(entries[d2].dl);
            }
        }
        dictionary_destroy(label_keys);
        query_target_release(qt);
    }
    else if(!r->group_by.r) {
        rrdr_free(owa, r);
        r = NULL;
    }

    onewayalloc_freez(owa, entries);
    dictionary_destroy(groups);

    return r;
}

static void rrd2rrdr_group_by_add_metric(RRDR *r, size_t query_metric_id) {
    if(!r->group_by.r)
        return;

    QUERY_TARGET *qt = r->internal.qt;
    RRDR_OPTIONS options = qt->request.options;
    RRDR *r_tmp = r->group_by.r;

    // do the group_by
    for(size_t i = 0; i != rrdr_rows(r_tmp) ; i++) {

        size_t idx_tmp = i * r_tmp->d;
        NETDATA_DOUBLE *cn_tmp_base = &r_tmp->v[ idx_tmp ];
        RRDR_VALUE_FLAGS *co_tmp_base = &r_tmp->o[ idx_tmp ];
        NETDATA_DOUBLE *ar_tmp_base = &r_tmp->ar[ idx_tmp ];

        size_t idx = i * r->d;
        NETDATA_DOUBLE *cn_base = &r->v[ idx ];
        RRDR_VALUE_FLAGS *co_base = &r->o[ idx ];
        NETDATA_DOUBLE *ar_base = &r->ar[ idx ];
        uint32_t *gbc_base = &r->gbc[ idx ];

        for(size_t d_tmp = 0; d_tmp < r_tmp->d ; d_tmp++) {
            if(unlikely(!(r_tmp->od[d_tmp] & RRDR_DIMENSION_QUERIED)))
                continue;

            NETDATA_DOUBLE n_tmp = cn_tmp_base[d_tmp];
            RRDR_VALUE_FLAGS o_tmp = co_tmp_base[d_tmp];
            NETDATA_DOUBLE ar_tmp = ar_tmp_base[d_tmp];

            if(o_tmp & RRDR_VALUE_EMPTY) {
                if(options & RRDR_OPTION_NULL2ZERO)
                    n_tmp = 0.0;
                else
                    continue;
            }

            if(unlikely((options & RRDR_OPTION_ABSOLUTE) && n_tmp < 0))
                n_tmp = -n_tmp;

            QUERY_METRIC *qm = query_metric(qt, query_metric_id);
            size_t d = qm->grouped_as.slot;

            r->od[d] |= RRDR_DIMENSION_QUERIED;

            NETDATA_DOUBLE *cn = &cn_base[d];
            RRDR_VALUE_FLAGS *co = &co_base[d];
            NETDATA_DOUBLE *ar = &ar_base[d];
            uint32_t *gbc = &gbc_base[d];

            switch(qt->request.group_by_aggregate_function) {
                default:
                case RRDR_GROUP_BY_FUNCTION_AVERAGE:
                case RRDR_GROUP_BY_FUNCTION_SUM:
                    *cn += n_tmp;
                    break;

                case RRDR_GROUP_BY_FUNCTION_MIN:
                    if(n_tmp < *cn)
                        *cn = n_tmp;
                    break;

                case RRDR_GROUP_BY_FUNCTION_MAX:
                    if(n_tmp > *cn)
                        *cn = n_tmp;
                    break;
            }

            *co |= (o_tmp & (RRDR_VALUE_RESET | RRDR_VALUE_PARTIAL));
            *ar += ar_tmp;
            (*gbc)++;
        }
    }
}

static void rrd2rrdr_group_by_finalize(RRDR *r) {
    if(!r->group_by.r)
        return;

    QUERY_TARGET *qt = r->internal.qt;
    RRDR_OPTIONS options = qt->request.options;

    // copy the timestamps
    for(size_t i = 0; i != r->n ;i++) {
        r->t[i] = r->group_by.r->t[i];
    }

    if(!(options & RRDR_OPTION_RETURN_RAW)) {
        // partial trimming
        size_t last_row_gbc = 0;
        for (size_t i = 0; i != r->n; i++) {
            size_t row_gbc = 0;
            for (size_t d = 0; d < r->d; d++) {
                if (unlikely(!(r->od[d] & RRDR_DIMENSION_QUERIED)))
                    continue;

                row_gbc += r->gbc[ i * r->d + d ];
            }

            if (unlikely(r->t[i] > r->partial_data_trimming.expected_after && row_gbc < last_row_gbc)) {
                // discard the rest of the points
                r->partial_data_trimming.trimmed_after = r->t[i];
                r->rows = i;
                break;
            }
            else
                last_row_gbc = row_gbc;
        }
    }

    // apply averaging, remove RRDR_VALUE_EMPTY, find the non-zero dimensions, min and max
    size_t min_max_values = 0;
    NETDATA_DOUBLE min = NAN, max = NAN;
    for (size_t d = 0; d < r->d; d++) {
        size_t non_zero = 0;

        NETDATA_DOUBLE sum = 0;
        size_t count = 0;

        for(size_t i = 0; i != r->n ;i++) {
            size_t idx2 = i * r->d + d;

            NETDATA_DOUBLE *cn2 = &r->v[ idx2 ];
            RRDR_VALUE_FLAGS *co2 = &r->o[ idx2 ];
            NETDATA_DOUBLE *ar2 = &r->ar[ idx2 ];
            uint32_t gbc2 = r->gbc[ idx2 ];

            if(likely(gbc2)) {
                *co2 &= ~RRDR_VALUE_EMPTY;

                if(gbc2 != r->dgbc[d])
                    *co2 |= RRDR_VALUE_PARTIAL;

                NETDATA_DOUBLE n;

                sum += *cn2;
                count += gbc2;

                if(qt->request.group_by_aggregate_function == RRDR_GROUP_BY_FUNCTION_AVERAGE)
                    n = (*cn2 /= gbc2);
                else
                    n = *cn2;

                if(!query_target_aggregatable(qt))
                    *ar2 /= gbc2;

                if(islessgreater(n, 0.0))
                    non_zero++;

                if(unlikely(!min_max_values++)) {
                    min = n;
                    max = n;
                }
                else {
                    if(n < min)
                        min = n;

                    if(n > max)
                        max = n;
                }
            }
        }

        if(non_zero)
            r->od[d] |= RRDR_DIMENSION_NONZERO;

        r->dv[d] = (count) ? sum / (NETDATA_DOUBLE)count : 0.0;
    }

    r->view.min = min;
    r->view.max = max;

    // update query instance counts in query host and query context
    {
        size_t h = 0, c = 0, i = 0;
        for(; h < qt->nodes.used ; h++) {
            QUERY_NODE *qn = &qt->nodes.array[h];

            for(; c < qt->contexts.used ;c++) {
                QUERY_CONTEXT *qc = &qt->contexts.array[c];

                if(!rrdcontext_acquired_belongs_to_host(qc->rca, qn->rrdhost))
                    break;

                for(; i < qt->instances.used ;i++) {
                    QUERY_INSTANCE *qi = &qt->instances.array[i];

                    if(!rrdinstance_acquired_belongs_to_context(qi->ria, qc->rca))
                        break;

                    if(qi->metrics.queried) {
                        qc->instances.queried++;
                        qn->instances.queried++;
                    }
                    else if(qi->metrics.failed) {
                        qc->instances.failed++;
                        qn->instances.failed++;
                    }
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// query entry point

RRDR *rrd2rrdr_legacy(
        ONEWAYALLOC *owa,
        RRDSET *st, size_t points, time_t after, time_t before,
        RRDR_TIME_GROUPING group_method, time_t resampling_time, RRDR_OPTIONS options, const char *dimensions,
        const char *group_options, time_t timeout, size_t tier, QUERY_SOURCE query_source,
        STORAGE_PRIORITY priority) {

    QUERY_TARGET_REQUEST qtr = {
            .version = 1,
            .st = st,
            .points = points,
            .after = after,
            .before = before,
            .time_group_method = group_method,
            .resampling_time = resampling_time,
            .options = options,
            .dimensions = dimensions,
            .time_group_options = group_options,
            .timeout = timeout,
            .tier = tier,
            .query_source = query_source,
            .priority = priority,
    };

    return rrd2rrdr(owa, query_target_create(&qtr));
}

RRDR *rrd2rrdr(ONEWAYALLOC *owa, QUERY_TARGET *qt) {
    if(!qt)
        return NULL;

    if(!owa) {
        query_target_release(qt);
        return NULL;
    }

    // qt.window members are the WANTED ones.
    // qt.request members are the REQUESTED ones.

    RRDR *r = rrd2rrdr_group_by_initialize(owa, qt);
    if(!r)
        return NULL;

    if(qt->window.relative)
        r->view.flags |= RRDR_RESULT_FLAG_RELATIVE;
    else
        r->view.flags |= RRDR_RESULT_FLAG_ABSOLUTE;

    // -------------------------------------------------------------------------
    // initialize RRDR

    r->view.group = qt->window.group;
    r->view.update_every = (int) (qt->window.group * qt->window.query_granularity);
    r->view.before = qt->window.before;
    r->view.after = qt->window.after;
    r->view.options = qt->window.options;
    r->time_grouping.points_wanted = qt->window.points;
    r->time_grouping.resampling_group = qt->window.resampling_group;
    r->time_grouping.resampling_divisor = qt->window.resampling_divisor;

    if(r->group_by.r) {
        r->group_by.r->view = r->view;
        r->group_by.r->time_grouping = r->time_grouping;
    }

    RRDR *r_tmp = r->group_by.r ? r->group_by.r : r;

    // -------------------------------------------------------------------------
    // assign the processor functions
    rrdr_set_grouping_function(r_tmp, qt->window.group_method);

    // allocate any memory required by the grouping method
    r_tmp->time_grouping.create(r_tmp, qt->window.group_options);

    // -------------------------------------------------------------------------
    // do the work for each dimension

    time_t max_after = 0, min_before = 0;
    size_t max_rows = 0;

    long dimensions_used = 0, dimensions_nonzero = 0;
    struct timeval query_start_time;
    struct timeval query_current_time;
    if (qt->request.timeout)
        now_realtime_timeval(&query_start_time);

    size_t last_db_points_read = 0;
    size_t last_result_points_generated = 0;

    internal_fatal(released_ops, "QUERY: released_ops should be NULL when the query starts");

    QUERY_ENGINE_OPS **ops = NULL;
    if(qt->query.used)
        ops = onewayalloc_callocz(owa, qt->query.used, sizeof(QUERY_ENGINE_OPS *));

    size_t capacity = libuv_worker_threads * 10;
    size_t max_queries_to_prepare = (qt->query.used > (capacity - 1)) ? (capacity - 1) : qt->query.used;
    size_t queries_prepared = 0;
    while(queries_prepared < max_queries_to_prepare) {
        // preload another query
        ops[queries_prepared] = rrd2rrdr_query_ops_prep(r_tmp, queries_prepared);
        queries_prepared++;
    }

    for(size_t d = 0; d < qt->query.used ; d++) {
        QUERY_METRIC *qm = query_metric(qt, d);
        QUERY_DIMENSION *qd = query_dimension(qt, qm->link.query_dimension_id);
        QUERY_INSTANCE *qi = query_instance(qt, qm->link.query_instance_id);
        QUERY_CONTEXT *qc = query_context(qt, qm->link.query_context_id);
        QUERY_NODE *qn = query_node(qt, qm->link.query_node_id);

        if(queries_prepared < qt->query.used) {
            // preload another query
            ops[queries_prepared] = rrd2rrdr_query_ops_prep(r_tmp, queries_prepared);
            queries_prepared++;
        }

        size_t dim_in_rrdr_tmp = (r_tmp != r) ? 0 : d;

        // set the query target dimension options to rrdr
        r_tmp->od[dim_in_rrdr_tmp] = qm->status;

        // reset the grouping for the new dimension
        r_tmp->time_grouping.reset(r_tmp);

        if(ops[d]) {
            rrd2rrdr_query_execute(r_tmp, dim_in_rrdr_tmp, ops[d]);
            r_tmp->od[dim_in_rrdr_tmp] |= RRDR_DIMENSION_QUERIED;

            if(r_tmp != r) {
                // copy back whatever got updated from the temporary r

                // the query updates RRDR_DIMENSION_NONZERO
                qm->status = r_tmp->od[dim_in_rrdr_tmp];

                // the query updates these
                r->view.min = r_tmp->view.min;
                r->view.max = r_tmp->view.max;
                r->view.after = r_tmp->view.after;
                r->view.before = r_tmp->view.before;
                r->rows = r_tmp->rows;

                rrd2rrdr_group_by_add_metric(r, d);
            }

            rrd2rrdr_query_ops_release(ops[d]); // reuse this ops allocation
            ops[d] = NULL;

            qi->metrics.queried++;
            qc->metrics.queried++;
            qn->metrics.queried++;

            qd->status |= QUERY_STATUS_QUERIED;
            qm->status |= RRDR_DIMENSION_QUERIED;

            if(qt->request.version >= 2) {
                query_target_merge_data_statistics(&qi->query_stats, &qm->query_stats);
                query_target_merge_data_statistics(&qc->query_stats, &qm->query_stats);
                query_target_merge_data_statistics(&qn->query_stats, &qm->query_stats);
                query_target_merge_data_statistics(&qt->query_stats, &qm->query_stats);
            }
        }
        else {
            qi->metrics.failed++;
            qc->metrics.failed++;
            qn->metrics.failed++;

            qd->status |= QUERY_STATUS_FAILED;
            qm->status |= RRDR_DIMENSION_FAILED;

            continue;
        }

        global_statistics_rrdr_query_completed(
                1,
                r_tmp->stats.db_points_read - last_db_points_read,
                r_tmp->stats.result_points_generated - last_result_points_generated,
                qt->request.query_source);

        last_db_points_read = r_tmp->stats.db_points_read;
        last_result_points_generated = r_tmp->stats.result_points_generated;

        if (qt->request.timeout)
            now_realtime_timeval(&query_current_time);

        if(qm->status & RRDR_DIMENSION_NONZERO)
            dimensions_nonzero++;

        // verify all dimensions are aligned
        if(unlikely(!dimensions_used)) {
            min_before = r->view.before;
            max_after = r->view.after;
            max_rows = r->rows;
        }
        else {
            if(r->view.after != max_after) {
                internal_error(true, "QUERY: 'after' mismatch between dimensions for chart '%s': max is %zu, dimension '%s' has %zu",
                               rrdinstance_acquired_id(qi->ria), (size_t)max_after, rrdmetric_acquired_id(qd->rma), (size_t)r->view.after);

                r->view.after = (r->view.after > max_after) ? r->view.after : max_after;
            }

            if(r->view.before != min_before) {
                internal_error(true, "QUERY: 'before' mismatch between dimensions for chart '%s': max is %zu, dimension '%s' has %zu",
                               rrdinstance_acquired_id(qi->ria), (size_t)min_before, rrdmetric_acquired_id(qd->rma), (size_t)r->view.before);

                r->view.before = (r->view.before < min_before) ? r->view.before : min_before;
            }

            if(r->rows != max_rows) {
                internal_error(true, "QUERY: 'rows' mismatch between dimensions for chart '%s': max is %zu, dimension '%s' has %zu",
                               rrdinstance_acquired_id(qi->ria), (size_t)max_rows, rrdmetric_acquired_id(qd->rma), (size_t)r->rows);

                r->rows = (r->rows > max_rows) ? r->rows : max_rows;
            }
        }

        dimensions_used++;

        bool cancel = false;
        if (qt->request.interrupt_callback && qt->request.interrupt_callback(qt->request.interrupt_callback_data)) {
            cancel = true;
            log_access("QUERY INTERRUPTED");
        }

        if (qt->request.timeout && ((NETDATA_DOUBLE)dt_usec(&query_start_time, &query_current_time) / 1000.0) > (NETDATA_DOUBLE)qt->request.timeout) {
            cancel = true;
            log_access("QUERY CANCELED RUNTIME EXCEEDED %0.2f ms (LIMIT %lld ms)",
                       (NETDATA_DOUBLE)dt_usec(&query_start_time, &query_current_time) / 1000.0, (long long)qt->request.timeout);
        }

        if(cancel) {
            r->view.flags |= RRDR_RESULT_FLAG_CANCEL;

            for(size_t i = d + 1; i < queries_prepared ; i++) {
                if(ops[i]) {
                    query_planer_finalize_remaining_plans(ops[i]);
                    rrd2rrdr_query_ops_release(ops[i]);
                    ops[i] = NULL;
                }
            }

            break;
        }
    }

    // free all resources used by the grouping method
    r_tmp->time_grouping.free(r_tmp);

    rrd2rrdr_group_by_finalize(r);

#ifdef NETDATA_INTERNAL_CHECKS
    if (dimensions_used && !(r->view.flags & RRDR_RESULT_FLAG_CANCEL)) {
        if(r->internal.log)
            rrd2rrdr_log_request_response_metadata(r, qt->window.options, qt->window.group_method, qt->window.aligned, qt->window.group, qt->request.resampling_time, qt->window.resampling_group,
                                                   qt->window.after, qt->request.after, qt->window.before, qt->request.before,
                                                   qt->request.points, qt->window.points, /*after_slot, before_slot,*/
                                                   r->internal.log);

        if(r->rows != qt->window.points)
            rrd2rrdr_log_request_response_metadata(r, qt->window.options, qt->window.group_method, qt->window.aligned, qt->window.group, qt->request.resampling_time, qt->window.resampling_group,
                                                   qt->window.after, qt->request.after, qt->window.before, qt->request.before,
                                                   qt->request.points, qt->window.points, /*after_slot, before_slot,*/
                                                   "got 'points' is not wanted 'points'");

        if(qt->window.aligned && (r->view.before % (qt->window.group * qt->window.query_granularity)) != 0)
            rrd2rrdr_log_request_response_metadata(r, qt->window.options, qt->window.group_method, qt->window.aligned, qt->window.group, qt->request.resampling_time, qt->window.resampling_group,
                                                   qt->window.after, qt->request.after, qt->window.before,qt->request.before,
                                                   qt->request.points, qt->window.points, /*after_slot, before_slot,*/
                                                   "'before' is not aligned but alignment is required");

        // 'after' should not be aligned, since we start inside the first group
        //if(qt->window.aligned && (r->after % group) != 0)
        //    rrd2rrdr_log_request_response_metadata(r, qt->window.options, qt->window.group_method, qt->window.aligned, qt->window.group, qt->request.resampling_time, qt->window.resampling_group, qt->window.after, after_requested, before_wanted, before_requested, points_requested, points_wanted, after_slot, before_slot, "'after' is not aligned but alignment is required");

        if(r->view.before != qt->window.before)
            rrd2rrdr_log_request_response_metadata(r, qt->window.options, qt->window.group_method, qt->window.aligned, qt->window.group, qt->request.resampling_time, qt->window.resampling_group,
                                                   qt->window.after, qt->request.after, qt->window.before, qt->request.before,
                                                   qt->request.points, qt->window.points, /*after_slot, before_slot,*/
                                                   "chart is not aligned to requested 'before'");

        if(r->view.before != qt->window.before)
            rrd2rrdr_log_request_response_metadata(r, qt->window.options, qt->window.group_method, qt->window.aligned, qt->window.group, qt->request.resampling_time, qt->window.resampling_group,
                                                   qt->window.after, qt->request.after, qt->window.before, qt->request.before,
                                                   qt->request.points, qt->window.points, /*after_slot, before_slot,*/
                                                   "got 'before' is not wanted 'before'");

        // reported 'after' varies, depending on group
        if(r->view.after != qt->window.after)
            rrd2rrdr_log_request_response_metadata(r, qt->window.options, qt->window.group_method, qt->window.aligned, qt->window.group, qt->request.resampling_time, qt->window.resampling_group,
                                                   qt->window.after, qt->request.after, qt->window.before, qt->request.before,
                                                   qt->request.points, qt->window.points, /*after_slot, before_slot,*/
                                                   "got 'after' is not wanted 'after'");

    }
#endif

    // free the query pipelining ops
    for(size_t d = 0; d < qt->query.used ; d++) {
        rrd2rrdr_query_ops_release(ops[d]);
        ops[d] = NULL;
    }
    rrd2rrdr_query_ops_freeall(r);
    internal_fatal(released_ops, "QUERY: released_ops should be NULL when the query ends");

    onewayalloc_freez(owa, ops);

    if(likely(dimensions_used)) {
        // when all the dimensions are zero, we should return all of them
        if (unlikely((qt->window.options & RRDR_OPTION_NONZERO) && !dimensions_nonzero &&
                     !(r->view.flags & RRDR_RESULT_FLAG_CANCEL))) {
            // all the dimensions are zero
            // mark them as NONZERO to send them all
            for (size_t d = 0; d < r->d; d++) {
                if (unlikely(r->od[d] & RRDR_DIMENSION_HIDDEN)) continue;
                if (unlikely(!(r->od[d] & RRDR_DIMENSION_QUERIED))) continue;
                r->od[d] |= RRDR_DIMENSION_NONZERO;
            }
        }
    }

    return r;
}
