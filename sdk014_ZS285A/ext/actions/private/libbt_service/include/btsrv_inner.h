/*
 * Copyright (c) 2016 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt service inner head file
 */

#ifndef _BTSRV_INNER_H_
#define _BTSRV_INNER_H_

#if defined(CONFIG_SYS_LOG)
#define SYS_LOG_NO_NEWLINE
#ifdef SYS_LOG_DOMAIN
#undef SYS_LOG_DOMAIN
#endif
#define SYS_LOG_DOMAIN "btsrv"
#include <logging/sys_log.h>
#endif

#include <btservice_api.h>
#include <msg_manager.h>
#include <srv_manager.h>
#include <thread_timer.h>
#include <../btsrv_config.h>
#include <bluetooth/host_interface.h>

#ifdef CONFIG_SUPPORT_TWS
#include <btsrv_tws.h>
#endif

/* Two phone device, one tws device */
#define BTSRV_SAVE_AUTOCONN_NUM					(3)

enum {
	LINK_ADJUST_IDLE,
	LINK_ADJUST_RUNNING,
	LINK_ADJUST_STOP,
	LINK_ADJUST_SINK_BLOCK,
	LINK_ADJUST_SINK_CLEAR_BLOCK,
};

typedef enum {
	/** first time reconnect */
	RECONN_STARTUP,
	/** reconnect when timeout */
	RECONN_TIMEOUT,
} btstack_reconnect_mode_e;

typedef void (*rdm_connected_dev_cb)(struct bt_conn *base_conn, u8_t tws_dev, void *cb_param);

#define MEDIA_RTP_HEAD_LEN		12
#define AVDTP_SBC_HEADER_LEN	13
#define AVDTP_AAC_HEADER_LEN	16

enum {
	MEDIA_RTP_HEAD	   = 0x80,
	MEDIA_RTP_TYPE_SBC = 0x60,
	MEDIA_RTP_TYPE_AAC = 0x80,		/* 0x80 or 0x62 ?? */
};

/** avrcp device state */
typedef enum {
	BTSRV_AVRCP_PLAYSTATUS_STOPPED,
	BTSRV_AVRCP_PLAYSTATUS_PAUSEED,
	BTSRV_AVRCP_PLAYSTATUS_PLAYING,
} btsrv_avrcp_state_e;

enum {
	BTSRV_LINK_BASE_CONNECTED,
	BTSRV_LINK_BASE_CONNECTED_FAILED,
	BTSRV_LINK_BASE_CONNECTED_TIMEOUT,
	BTSRV_LINK_BASE_DISCONNECTED,
	BTSRV_LINK_BASE_GET_NAME,
	BTSRV_LINK_HFP_CONNECTED,
	BTSRV_LINK_HFP_DISCONNECTED,
	BTSRV_LINK_A2DP_CONNECTED,
	BTSRV_LINK_A2DP_DISCONNECTED,
	BTSRV_LINK_AVRCP_CONNECTED,
	BTSRV_LINK_AVRCP_DISCONNECTED,
	BTSRV_LINK_SPP_CONNECTED,
	BTSRV_LINK_SPP_DISCONNECTED,
	BTSRV_LINK_PBAP_CONNECTED,
	BTSRV_LINK_PBAP_DISCONNECTED,
	BTSRV_LINK_HID_CONNECTED,
	BTSRV_LINK_HID_DISCONNECTED,
	BTSRV_LINK_MAP_CONNECTED,
	BTSRV_LINK_MAP_DISCONNECTED,
};

struct btsrv_info_t {
	u8_t running:1;
	u8_t allow_sco_connect:1;
	btsrv_callback callback;
	u8_t device_name[CONFIG_MAX_BT_NAME_LEN + 1];
	u8_t device_addr[6];
	struct btsrv_config_info cfg;
	struct thread_timer wait_disconnect_timer;
};

struct btsrv_addr_name {
	bd_address_t mac;
	u8_t name[CONFIG_MAX_BT_NAME_LEN + 1];
};

struct btsrv_conn_rsp_t {
	struct bt_conn *conn;
	u16_t psm;
	u32_t waittime;
};

extern struct btsrv_info_t *btsrv_info;
#define btsrv_max_conn_num()                    btsrv_info->cfg.max_conn_num
#define btsrv_max_phone_num()                   btsrv_info->cfg.max_phone_num
#define btsrv_is_pts_test()                     btsrv_info->cfg.pts_test_mode
#define btsrv_is_preemption_mode()              btsrv_info->cfg.double_preemption_mode
#define btsrv_volume_sync_delay_ms()            btsrv_info->cfg.volume_sync_delay_ms
#define btsrv_get_tws_version()                 btsrv_info->cfg.tws_version
#define btsrv_get_tws_feature()                 btsrv_info->cfg.tws_feature

struct btsrv_info_t *btsrv_adapter_init(btsrv_callback cb);
int btsrv_adapter_process(struct app_msg *msg);
int btsrv_adapter_callback(btsrv_event_e event, void *param);
void btsrv_adapter_run(void);
int btsrv_adapter_stop(void);
int btsrv_adapter_set_config_info(void *param);
int btsrv_adapter_start_discover(struct btsrv_discover_param *param);
int btsrv_adapter_stop_discover(void);
int btsrv_adapter_connect(bd_address_t *addr);
int btsrv_adapter_check_cancal_connect(bd_address_t *addr);
int btsrv_adapter_disconnect(struct bt_conn *conn);
int btsrv_adapter_set_discoverable(bool enable);
int btsrv_adapter_set_connnectable(bool enable);

int btsrv_a2dp_process(struct app_msg *msg);
int btsrv_a2dp_init(struct btsrv_a2dp_start_param *param);
int btsrv_a2dp_deinit(void);
int btsrv_a2dp_disconnect(struct bt_conn *conn);
int btsrv_a2dp_connect(struct bt_conn *conn, u8_t role);
void btsrv_a2dp_halt_aac_endpoint(bool halt);

int btsrv_a2dp_media_state_change(struct bt_conn *conn, u8_t state);
int btsrv_a2dp_media_parser_frame_info(u8_t codec_id, u8_t *data, u32_t data_len, u16_t *frame_cnt, u16_t *frame_len);
u32_t btsrv_a2dp_media_cal_frame_time_us(u8_t codec_id, u8_t *data);
u16_t btsrv_a2dp_media_cal_frame_samples(u8_t codec_id, u8_t *data);
u8_t btsrv_a2dp_media_get_samples_rate(u8_t codec_id, u8_t *data);
u8_t *btsrv_a2dp_media_get_zero_frame(u8_t codec_id, u16_t *len, u8_t sample_rate);

struct btsrv_rdm_avrcp_pass_info {
	u8_t pass_state;
	u8_t op_id;
	u32_t op_time;
};

int btsrv_avrcp_process(struct app_msg *msg);
int btsrv_avrcp_init(btsrv_avrcp_callback_t *cb);
int btsrv_avrcp_deinit(void);
int btsrv_avrcp_disconnect(struct bt_conn *conn);
int btsrv_avrcp_connect(struct bt_conn *conn);
void btsrv_avrcp_preemption_pause(struct bt_conn *conn);
void btsrv_avrcp_resume_preemption_play(struct bt_conn *conn);

typedef enum {
	BTSRV_SCO_STATE_INIT,
	BTSRV_SCO_STATE_PHONE,
	BTSRV_SCO_STATE_HFP,
	BTSRV_SCO_STATE_DISCONNECT,
} btsrv_sco_state;

typedef enum {
	BTSRV_HFP_ROLE_HF =0,
	BTSRV_HFP_ROLE_AG,
} btsrv_hfp_role;

int btsrv_hfp_process(struct app_msg *msg);
int btsrv_hfp_init(btsrv_hfp_callback cb);
int btsrv_hfp_deinit(void);
int btsrv_hfp_disconnect(struct bt_conn *conn);
int btsrv_hfp_connect(struct bt_conn *conn);
int btsrv_hfp_set_status(struct bt_conn *conn, int state);
int btsrv_hfp_get_status(void);
u8_t btsrv_hfp_get_codec_id(struct bt_conn *conn);
int btsrv_hfp_get_call_state(u8_t active_call);
int btsrv_sco_process(struct app_msg *msg);
struct bt_conn *btsrv_sco_get_conn(void);
void btsrv_sco_disconnect(struct bt_conn *sco_conn);

int btsrv_hfp_ag_process(struct app_msg *msg);
int btsrv_hfp_ag_connect(struct bt_conn *conn);
int btsrv_hfp_ag_set_status(struct bt_conn *conn, int state);

int btsrv_spp_send_data(u8_t app_id, u8_t *data, u32_t len);
u8_t btsrv_spp_get_rfcomm_channel(u8_t app_id);
int btsrv_spp_process(struct app_msg *msg);

int btsrv_pbap_process(struct app_msg *msg);
int btsrv_map_process(struct app_msg *msg);

void btsrv_hid_connect(struct bt_conn *conn);
int btsrv_hid_process(struct app_msg *msg);

bool btsrv_rdm_need_high_performance(void);
struct bt_conn *btsrv_rdm_find_conn_by_addr(bd_address_t *addr);
int btsrv_rdm_get_connected_dev(rdm_connected_dev_cb cb, void *cb_param);
int btsrv_rdm_get_dev_state(struct bt_dev_rdm_state *state);
int btsrv_rdm_get_connected_dev_cnt_by_type(int type);
int btsrv_rdm_get_autoconn_dev(struct autoconn_info *info, int max_dev);
int btsrv_rdm_base_disconnected(struct bt_conn *base_conn);
int btsrv_rdm_add_dev(struct bt_conn *base_conn);
int btsrv_rdm_remove_dev(u8_t *mac);
void btsrv_rdm_set_security_changed(struct bt_conn *base_conn);
bool btsrv_rdm_is_security_changed(struct bt_conn *base_conn);
bool btsrv_rdm_is_connected(struct bt_conn *base_conn);
bool btsrv_rdm_is_a2dp_connected(struct bt_conn *base_conn);
bool btsrv_rdm_is_avrcp_connected(struct bt_conn *base_conn);
bool btsrv_rdm_is_hfp_connected(struct bt_conn *base_conn);
bool btsrv_rdm_is_spp_connected(struct bt_conn *base_conn);
bool btsrv_rdm_is_hid_connected(struct bt_conn *base_conn);
int btsrv_rdm_set_a2dp_connected(struct bt_conn *base_conn, bool connected);
int btsrv_rdm_a2dp_actived_switch_lock(struct bt_conn *base_conn, u8_t lock);
int btsrv_rdm_a2dp_actived(struct bt_conn *base_conn, u8_t actived);
struct bt_conn *btsrv_rdm_a2dp_get_actived(void);
struct bt_conn *btsrv_rdm_a2dp_get_second_dev(void);
int btsrv_rdm_is_actived_a2dp_stream_open(void);
int btsrv_rdm_is_a2dp_stream_open(struct bt_conn *base_conn);
int btsrv_rdm_a2dp_set_codec_info(struct bt_conn *base_conn, u8_t format, u8_t sample_rate, u8_t cp_type);
int btsrv_rdm_a2dp_get_codec_info(struct bt_conn *base_conn, u8_t *format, u8_t *sample_rate, u8_t *cp_type);
int btsrv_rdm_get_a2dp_start_time(struct bt_conn *base_conn, u32_t *start_time);
void btsrv_rdm_get_a2dp_acitve_mac(bd_address_t *addr);
int btsrv_rdm_set_a2dp_pending_ahead_start(struct bt_conn *base_conn, u8_t start);
u8_t btsrv_rdm_get_a2dp_pending_ahead_start(struct bt_conn *base_conn);
int btsrv_rdm_set_avrcp_connected(struct bt_conn *base_conn, bool connected);
u8_t btsrv_rdm_avrcp_set_play_status(struct bt_conn *base_conn, u8_t status);
u8_t btsrv_rdm_avrcp_get_play_status(struct bt_conn *base_conn);
u32_t btsrv_rdm_avrcp_get_pause_time(struct bt_conn *base_conn);
void *btsrv_rdm_avrcp_get_pass_info(struct bt_conn *base_conn);
struct bt_conn *btsrv_rdm_avrcp_get_connected_dev(void);
int btsrv_rdm_set_hfp_connected(struct bt_conn *base_conn, bool connected);
int btsrv_rdm_set_hfp_role(struct bt_conn *base_conn, u8_t role);
int btsrv_rdm_get_hfp_role(struct bt_conn *base_conn);
int btsrv_rdm_hfp_actived(struct bt_conn *base_conn, u8_t actived, u8_t force);
struct bt_conn *btsrv_rdm_hfp_get_actived_sco(void);
int btsrv_rdm_hfp_set_codec_info(struct bt_conn *base_conn, u8_t format, u8_t sample_rate);
int btsrv_rdm_hfp_set_state(struct bt_conn *base_conn, u8_t state);
int btsrv_rdm_hfp_get_state(struct bt_conn *base_conn);
int btsrv_rdm_hfp_set_sco_state(struct bt_conn *base_conn, u8_t state);
int btsrv_rdm_hfp_get_sco_state(struct bt_conn *base_conn);
int btsrv_rdm_hfp_set_call_state(struct bt_conn *base_conn, u8_t active, u8_t held, u8_t in, u8_t out);
int btsrv_rdm_hfp_get_call_state(struct bt_conn *base_conn, u8_t *active, u8_t *held, u8_t *in, u8_t *out);
int btsrv_rdm_hfp_get_codec_info(struct bt_conn *base_conn, u8_t *format, u8_t *sample_rate);
int btsrv_rdm_hfp_set_notify_phone_num_state(struct bt_conn *base_conn, u8_t state);
int btsrv_rdm_hfp_get_notify_phone_num_state(struct bt_conn *base_conn);
struct bt_conn *btsrv_rdm_hfp_get_actived(void);
struct bt_conn *btsrv_rdm_hfp_get_second_dev(void);
void btsrv_rdm_get_hfp_acitve_mac(bd_address_t *addr);
struct bt_conn *btsrv_rdm_get_base_conn_by_sco(struct bt_conn *sco_conn);
int btsrv_rdm_sco_connected(struct bt_conn *base_conn, struct bt_conn *sco_conn);
int btsrv_rdm_sco_disconnected(struct bt_conn *sco_conn);
int btsrv_rdm_set_spp_connected(struct bt_conn *base_conn, bool connected);
int btsrv_rdm_set_pbap_connected(struct bt_conn *base_conn, bool connected);
int btsrv_rdm_set_map_connected(struct bt_conn *base_conn, bool connected);
int btsrv_rdm_set_hid_connected(struct bt_conn *base_conn, bool connected);
struct bt_conn *btsrv_rdm_hid_get_actived(void);
int btsrv_rdm_set_tws_role(struct bt_conn *base_conn, u8_t role);
struct bt_conn *btsrv_rdm_get_tws_by_role(u8_t role);
int btsrv_rdm_get_conn_role(struct bt_conn *base_conn);
int btsrv_rdm_get_dev_role(void);
int btsrv_rdm_set_controler_role(struct bt_conn *base_conn, u8_t role);
int btsrv_rdm_get_controler_role(struct bt_conn *base_conn, u8_t *role);
int btsrv_rdm_set_link_time(struct bt_conn *base_conn, u16_t link_time);
u16_t btsrv_rdm_get_link_time(struct bt_conn *base_conn);
void btsrv_rdm_set_dev_name(struct bt_conn *base_conn, u8_t *name);
u8_t *btsrv_rdm_get_dev_name(struct bt_conn *base_conn);
void btsrv_rdm_set_wait_to_diconnect(struct bt_conn *base_conn, bool set);
bool btsrv_rdm_is_wait_to_diconnect(struct bt_conn *base_conn);
void btsrv_rdm_set_switch_sbc_state(struct bt_conn *base_conn, u8_t state);
u8_t btsrv_rdm_get_switch_sbc_state(struct bt_conn *base_conn);
int btsrv_rdm_init(void);
void btsrv_rdm_deinit(void);
void btsrv_rdm_dump_info(void);

struct thread_timer * btsrv_rdm_get_sco_disconnect_timer(struct bt_conn *base_conn);
int btsrv_rdm_get_sco_creat_time(struct bt_conn *base_conn, u32_t *creat_time);

void btsrv_autoconn_info_update(void);
int btsrv_connect_get_auto_reconnect_info(struct autoconn_info *info, u8_t max_cnt);
void btsrv_connect_set_auto_reconnect_info(struct autoconn_info *info, u8_t max_cnt);
void btsrv_connect_set_phone_controler_role(bd_address_t *bd, u8_t role);
int btsrv_connect_process(struct app_msg *msg);
bool btsrv_autoconn_is_reconnecting(void);
bool btsrv_autoconn_is_runing(void);
void btsrv_connect_tws_role_confirm(void);
int btsrv_connect_init(void);
void btsrv_connect_deinit(void);
void btsrv_connect_dump_info(void);

void btsrv_scan_set_param(struct bt_scan_parameter *param, bool enhance_param);
void btsrv_scan_set_user_discoverable(bool enable, bool immediate);
void btsrv_scan_set_user_connectable(bool enable, bool immediate);
void btsrv_inner_set_scan_enable(bool discoverable, bool connectable);
void btsrv_scan_update_mode(bool immediate);
u8_t btsrv_scan_get_inquiry_mode(void);
int btsrv_scan_init(void);
void btsrv_scan_deinit(void);
void btsrv_scan_dump_info(void);

int btsrv_link_adjust_set_tws_state(u8_t adjust_state, u16_t source_cache, u16_t source_max_len, u16_t source_min_len, u16_t cache_sink);
int btsrv_link_adjust_tws_set_bt_play(bool bt_play);
int btsrv_link_adjust_init(void);
void btsrv_link_adjust_deinit(void);

bd_address_t *GET_CONN_BT_ADDR(struct bt_conn *conn);
u32_t bt_rand32_get(void);
int btsrv_set_negative_prio(void);
void btsrv_revert_prio(int prio);
int btsrv_property_set(const char *key, char *value, int value_len);
int btsrv_property_get(const char *key, char *value, int value_len);


#define BTSTAK_READY 0

enum {
	MSG_BTSRV_BASE = MSG_APP_MESSAGE_START,
	MSG_BTSRV_CONNECT,
	MSG_BTSRV_A2DP,
	MSG_BTSRV_AVRCP,
	MSG_BTSRV_HFP,
	MSG_BTSRV_HFP_AG,
	MSG_BTSRV_SCO,
	MSG_BTSRV_SPP,
	MSG_BTSRV_PBAP,
	MSG_BTSRV_HID,
	MSG_BTSRV_TWS,
	MSG_BTSRV_MAP,	
	MSG_BTSRV_MAX,
};

enum {
	MSG_BTSRV_SET_DEFAULT_SCAN_PARAM,
	MSG_BTSRV_SET_ENHANCE_SCAN_PARAM,
	MSG_BTSRV_SET_DISCOVERABLE,
	MSG_BTSRV_SET_CONNECTABLE,
	MSG_BTSRV_AUTO_RECONNECT,
	MSG_BTSRV_AUTO_RECONNECT_STOP,
	MSG_BTSRV_CONNECT_TO,
	MSG_BTSRV_DISCONNECT,
	MSG_BTSRV_READY,
	MSG_BTSRV_REQ_FLUSH_NVRAM,
	MSG_BTSRV_CONNECTED,
	MSG_BTSRV_CONNECTED_FAILED,
	MSG_BTSRV_SECURITY_CHANGED,
	MSG_BTSRV_ROLE_CHANGE,
	MSG_BTSRV_DISCONNECTED,
	MSG_BTSRV_DISCONNECTED_REASON,
	MSG_BTSRV_GET_NAME_FINISH,
	MSG_BTSRV_CLEAR_LIST_CMD,
	MGS_BTSRV_CLEAR_AUTO_INFO,
	MSG_BTSRV_DISCONNECT_DEVICE,
	MSG_BTSRV_ALLOW_SCO_CONNECT,

	MSG_BTSRV_A2DP_START,
	MSG_BTSRV_A2DP_STOP,
	MSG_BTSRV_A2DP_CHECK_STATE,
	MSG_BTSRV_A2DP_CONNECT_TO,
	MSG_BTSRV_A2DP_DISCONNECT,
	MSG_BTSRV_A2DP_CONNECTED,
	MSG_BTSRV_A2DP_DISCONNECTED,
	MSG_BTSRV_A2DP_MEDIA_STATE_OPEN,
	MSG_BTSRV_A2DP_MEDIA_STATE_START,
	MSG_BTSRV_A2DP_MEDIA_STATE_CLOSE,
	MSG_BTSRV_A2DP_MEDIA_STATE_SUSPEND,
	MSG_BTSRV_A2DP_ACTIVED_DEV_CHANGED,
	MSG_BTSRV_A2DP_SEND_DELAY_REPORT,
	MSG_BTSRV_A2DP_CHECK_SWITCH_SBC,

	MSG_BTSRV_AVRCP_START,
	MSG_BTSRV_AVRCP_STOP,
	MSG_BTSRV_AVRCP_CONNECT_TO,
	MSG_BTSRV_AVRCP_DISCONNECT,
	MSG_BTSRV_AVRCP_CONNECTED,
	MSG_BTSRV_AVRCP_DISCONNECTED,
	MSG_BTSRV_AVRCP_SEND_CMD,
	MSG_BTSRV_AVRCP_PLAYBACK_STATUS_STOPPED,
	MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PLAYING,
	MSG_BTSRV_AVRCP_PLAYBACK_STATUS_PAUSED,
	MSG_BTSRV_AVRCP_PLAYBACK_STATUS_FWD_SEEK,
	MSG_BTSRV_AVRCP_PLAYBACK_STATUS_REV_SEEK,
	MSG_BTSRV_AVRCP_PLAYBACK_STATUS_ERROR,
	MSG_BTSRV_AVRCP_PLAYBACK_TRACK_CHANGE,
	MSG_BTSRV_AVRCP_SYNC_VOLUME,
	MSG_BTSRV_AVRCP_GET_ID3_INFO,
	MSG_BTSRV_AVRCP_GET_PLAY_STATUS,
	MSG_BTSRV_AVRCP_SET_ABSOLUTE_VOLUME,

	MSG_BTSRV_HFP_START,
	MSG_BTSRV_HFP_STOP,
	MSG_BTSRV_HFP_CONNECTED,
	MSG_BTSRV_HFP_DISCONNECTED,
	MSG_BTSRV_HFP_SET_STATE,
	MSG_BTSRV_HFP_VOLUME_CHANGE,
	MSG_BTSRV_HFP_PHONE_NUM,
	MSG_BTSRV_HFP_CODEC_INFO,
	MSG_BTSRV_HFP_SIRI_STATE,
	MSG_BTSRV_SCO_START,
	MSG_BTSRV_SCO_STOP,
	MSG_BTSRV_SCO_CONNECTED,
	MSG_BTSRV_SCO_DISCONNECTED,

	MSG_BTSRV_HFP_SWITCH_SOUND_SOURCE,
	MSG_BTSRV_HFP_HF_DIAL_NUM,
	MSG_BTSRV_HFP_HF_DIAL_LAST_NUM,
	MSG_BTSRV_HFP_HF_DIAL_MEMORY,
	MSG_BTSRV_HFP_HF_VOLUME_CONTROL,
	MSG_BTSRV_HFP_HF_ACCEPT_CALL,
	MSG_BTSRV_HFP_HF_BATTERY_REPORT,
	MSG_BTSRV_HFP_HF_REJECT_CALL,
	MSG_BTSRV_HFP_HF_HANGUP_CALL,
	MSG_BTSRV_HFP_HF_HANGUP_ANOTHER_CALL,
	MSG_BTSRV_HFP_HF_HOLDCUR_ANSWER_CALL,
	MSG_BTSRV_HFP_HF_HANGUPCUR_ANSWER_CALL,
	MSG_BTSRV_HFP_HF_VOICE_RECOGNITION_START,
	MSG_BTSRV_HFP_HF_VOICE_RECOGNITION_STOP,
	MSG_BTSRV_HFP_HF_VOICE_SEND_AT_COMMAND,
	MSG_BTSRV_HFP_ACTIVED_DEV_CHANGED,
	MSG_BTSRV_HFP_GET_TIME,
	MSG_BTSRV_HFP_TIME_UPDATE,

	MSG_BTSRV_HFP_AG_START,
	MSG_BTSRV_HFP_AG_STOP,
	MSG_BTSRV_HFP_AG_CONNECTED,
	MSG_BTSRV_HFP_AG_DISCONNECTED,
	MSG_BTSRV_HFP_AG_UPDATE_INDICATOR,
	MSG_BTSRV_HFP_AG_SEND_EVENT,

	MSG_BTSRV_SPP_START,
	MSG_BTSRV_SPP_STOP,
	MSG_BTSRV_SPP_REGISTER,
	MSG_BTSRV_SPP_CONNECT,
	MSG_BTSRV_SPP_DISCONNECT,
	MSG_BTSRV_SPP_CONNECT_FAILED,
	MSG_BTSRV_SPP_CONNECTED,
	MSG_BTSRV_SPP_DISCONNECTED,

	MSG_BTSRV_PBAP_CONNECT_FAILED,
	MSG_BTSRV_PBAP_CONNECTED,
	MSG_BTSRV_PBAP_DISCONNECTED,
	MSG_BTSRV_PBAP_GET_PB,
	MSG_BTSRV_PBAP_ABORT_GET,

	MSG_BTSRV_HID_START,
	MSG_BTSRV_HID_STOP,
	MSG_BTSRV_HID_CONNECTED,
	MSG_BTSRV_HID_DISCONNECTED,
	MSG_BTSRV_HID_REGISTER,
	MSG_BTSRV_HID_CONNECT,
	MSG_BTSRV_HID_DISCONNECT,
	MSG_BTSRV_HID_SEND_CTRL_DATA,
	MSG_BTSRV_HID_SEND_INTR_DATA,
	MSG_BTSRV_HID_SEND_RSP,
	MSG_BTSRV_HID_UNPLUG,
	MSG_BTSRV_DID_REGISTER,

	MSG_BTSRV_TWS_INIT,
	MSG_BTSRV_TWS_DEINIT,
	MSG_BTSRV_TWS_VERSION_NEGOTIATE,
	MSG_BTSRV_TWS_ROLE_NEGOTIATE,
	MSG_BTSRV_TWS_NEGOTIATE_SET_ROLE,
	MSG_BTSRV_TWS_CONNECTED,
	MSG_BTSRV_TWS_DISCONNECTED,
	MSG_BTSRV_TWS_DISCONNECTED_ADDR,
	MSG_BTSRV_TWS_WAIT_PAIR,
	MSG_BTSRV_TWS_CANCEL_WAIT_PAIR,
	MSG_BTSRV_TWS_DISCOVERY_RESULT,
	MSG_BTSRV_TWS_DISCONNECT,
	MSG_BTSRV_TWS_RESTART,
	MSG_BTSRV_TWS_PROTOCOL_DATA,
	MSG_BTSRV_TWS_EVENT_SYNC,
	MSG_BTSRV_TWS_SCO_DATA,

	MSG_BTSRV_MAP_CONNECT,
	MSG_BTSRV_MAP_DISCONNECT,
	MSG_BTSRV_MAP_SET_FOLDER,
	MSG_BTSRV_MAP_GET_FOLDERLISTING,	
	MSG_BTSRV_MAP_GET_MESSAGESLISTING,		
	MSG_BTSRV_MAP_GET_MESSAGE,	
	MSG_BTSRV_MAP_ABORT_GET,	
	MSG_BTSRV_MAP_CONNECT_FAILED,
	MSG_BTSRV_MAP_CONNECTED,
	MSG_BTSRV_MAP_DISCONNECTED,   
	MSG_BTSRV_DUMP_INFO,

	/* Ext add */
	MSG_BTSRV_CONN_RSP_RESULT,
};

static inline btsrv_callback _btsrv_get_msg_param_callback(struct app_msg *msg)
{
	return (btsrv_callback)msg->ptr;
}

static inline int _btsrv_get_msg_param_type(struct app_msg *msg)
{
	return msg->type;
}

static inline int _btsrv_get_msg_param_cmd(struct app_msg *msg)
{
	return msg->cmd;
}

static inline int _btsrv_get_msg_param_reserve(struct app_msg *msg)
{
	return msg->reserve;
}

static inline void *_btsrv_get_msg_param_ptr(struct app_msg *msg)
{
	return msg->ptr;
}

static inline int _btsrv_get_msg_param_value(struct app_msg *msg)
{
	return msg->value;
}

int btsrv_function_call(int func_type, int cmd, void *param);
int btsrv_event_notify(int event_type, int cmd, void *param);
int btsrv_event_notify_ext(int event_type, int cmd, void *param, u8_t code);
int btsrv_event_notify_malloc(int event_type, int cmd, u8_t *data, u16_t len, u8_t code);
#define btsrv_function_call_malloc 		btsrv_event_notify_malloc

typedef int (*btsrv_msg_process)(struct app_msg *msg);

void bt_service_set_bt_ready(void);
int btsrv_register_msg_processer(u8_t msg_type, btsrv_msg_process processer);

int btsrv_storage_init(void);
int btsrv_storage_deinit(void);
int btsrv_storage_get_linkkey(struct bt_linkkey_info *info, u8_t cnt);
int btsrv_storage_update_linkkey(struct bt_linkkey_info *info, u8_t cnt);
int btsrv_storage_write_ori_linkkey(bd_address_t *addr, u8_t *link_key);
void btsrv_storage_clean_linkkey(void);

int btsrv_pts_send_hfp_cmd(char *cmd);
int btsrv_pts_hfp_active_connect_sco(void);
int btsrv_pts_avrcp_get_play_status(void);
int btsrv_pts_avrcp_pass_through_cmd(u8_t opid);
int btsrv_pts_avrcp_notify_volume_change(u8_t volume);
int btsrv_pts_avrcp_reg_notify_volume_change(void);
int btsrv_pts_register_auth_cb(bool reg_auth);
#endif
