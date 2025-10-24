
// SPDX-License-Identifier: GPL-2.0-only
/*
 */

#include "common.h"
#include <linux/qcom_scmi_vendor.h>

#define SCMI_VENDOR_MSG_MODULE_START   (16)
#define MAX_MASTERS 3
#define MAX_BUCKETS 3

/* BWPROF specific parameter IDs */
enum bwprof_param_id {
    BWPROF_SET_LOG_LEVEL = SCMI_VENDOR_MSG_MODULE_START,
    BWPROF_SET_SAMPLE_MS,
    BWPROF_MASTER_LIST,
    BWPROF_SET_ENABLE,
    BWPROF_SET_HIST_INFO
};

/* Algorithm strings for different operations */
#define BWPROF_ALGO_BASIC_MONITORING   0x01
#define BWPROF_ALGO_HISTOGRAM          0x02

struct master_info {
    uint8_t cnt;
    uint8_t masters[MAX_MASTERS];
} __packed;

struct bucket_info {
    uint32_t buckets[MAX_BUCKETS];
} __packed;

struct sample_ms_info {
    uint8_t hist;
    uint16_t sample_ms;
} __packed;

static int scmi_bwprof_set_param(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
                 u32 param_id, size_t size)
{
    struct scmi_xfer *t;
    int ret;
    void *msg;

    ret = ph->xops->xfer_get_init(ph, param_id, size, &t);
    if (ret)
        return ret;

    msg = t->tx.buf;
    memcpy(msg, buf, size);
    
    ret = ph->xops->do_xfer(ph, t);
    ph->xops->xfer_put(ph, t);

    return ret;
}

static int scmi_bwprof_get_param(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
                 u32 param_id, size_t tx_size, size_t rx_size)
{
    /* BWPROF protocol currently doesn't support get operations */
    return -EOPNOTSUPP;
}

static int scmi_bwprof_start_activity(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
                      u32 param_id, size_t size)
{
    /* For BWPROF, starting activity means enabling sampling */
    u8 enable_cmd = 1;
    return scmi_bwprof_set_param(ph, &enable_cmd, algo_str, BWPROF_SET_ENABLE, sizeof(enable_cmd));
}

static int scmi_bwprof_stop_activity(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
                     u32 param_id, size_t size)
{
    /* For BWPROF, stopping activity means disabling sampling */
    u8 enable_cmd = 0;
    return scmi_bwprof_set_param(ph, &enable_cmd, algo_str, BWPROF_SET_ENABLE, sizeof(enable_cmd));
}

/* Helper functions for specific BWPROF operations */
int bwprof_set_log_level(const struct scmi_protocol_handle *ph, u8 log_level, u64 algo_str)
{
    return scmi_bwprof_set_param(ph, &log_level, algo_str, 
                                BWPROF_SET_LOG_LEVEL, sizeof(log_level));
}

int bwprof_set_sampling_enable(const struct scmi_protocol_handle *ph, u8 enable, u64 algo_str)
{
    return scmi_bwprof_set_param(ph, &enable, algo_str,
                                BWPROF_SET_ENABLE, sizeof(enable));
}

int bwprof_set_hist_info(const struct scmi_protocol_handle *ph, u32 *buckets_list, u64 algo_str)
{
    struct bucket_info bucket_info;
    int i;

    for (i = 0; i < MAX_BUCKETS; i++)
        bucket_info.buckets[i] = cpu_to_le32(buckets_list[i]);

    return scmi_bwprof_set_param(ph, &bucket_info, algo_str,
                                BWPROF_SET_HIST_INFO, sizeof(bucket_info));
}

int bwprof_set_masters_list(const struct scmi_protocol_handle *ph, u8 cnt, u8 *master_list, u64 algo_str)
{
    struct master_info master_info;
    int i;

    if (cnt > MAX_MASTERS)
        return -EINVAL;

    master_info.cnt = cnt;
    for (i = 0; i < cnt; i++)
        master_info.masters[i] = master_list[i];

    return scmi_bwprof_set_param(ph, &master_info, algo_str,
                                BWPROF_MASTER_LIST, sizeof(master_info));
}

int bwprof_set_sample_ms(const struct scmi_protocol_handle *ph, uint8_t hist_enable, uint16_t ms_val)
{
    struct sample_ms_info sample_info;
    u64 algo_str;

    sample_info.hist = hist_enable;
    sample_info.sample_ms = ms_val;
    
    algo_str = hist_enable ? BWPROF_ALGO_HISTOGRAM : BWPROF_ALGO_BASIC_MONITORING;

    return scmi_bwprof_set_param(ph, &sample_info, algo_str,
                                BWPROF_SET_SAMPLE_MS, sizeof(sample_info));
}

/* SCMI v2 vendor ops */
static struct qcom_scmi_vendor_ops vendor_proto_ops = {
    .set_param = scmi_bwprof_set_param,
    .get_param = scmi_bwprof_get_param,
    .start_activity = scmi_bwprof_start_activity,
    .stop_activity = scmi_bwprof_stop_activity,
};

static int scmi_bwprof_vendor_protocol_init(const struct scmi_protocol_handle *ph)
{
    u32 version;

    ph->xops->version_get(ph, &version);
    dev_dbg(ph->dev, "QCOM SCMI vendor protocol version %d.%d\n",
            PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

    return 0;
}

static const struct scmi_protocol scmi_qcom_vendor = {
    .id = QCOM_SCMI_VENDOR_PROTOCOL,
    .owner = THIS_MODULE,
    .init_instance = scmi_bwprof_vendor_protocol_init,
    .ops = &vendor_proto_ops,
};

module_scmi_protocol(scmi_qcom_vendor);

MODULE_DESCRIPTION("QCOM SCMI Vendor Protocol with BWPROF Support");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL_GPL(bwprof_set_log_level);
EXPORT_SYMBOL_GPL(bwprof_set_sampling_enable);
EXPORT_SYMBOL_GPL(bwprof_set_hist_info);
EXPORT_SYMBOL_GPL(bwprof_set_masters_list);
EXPORT_SYMBOL_GPL(bwprof_set_sample_ms);