/*
 * Copyright (c) 2017 Actions Semi Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.

 * Author: jpl<jianpingliao@actions-semi.com>
 *
 * Change log:
 *	2019/12/16: Created by jpl.
 */
 
#ifndef __AS_DAE_SETTING_H__
#define __AS_DAE_SETTING_H__






typedef struct{
	int sample_rate;
	//0: blocksize*4, 1: blocksize*4, 2: blocksize *2
    int dae_buf_addr[3];     //帧处理buf，分左右声道，32bit位宽，长度分别是采样对数的4倍，传进来
    short dae_enable;        //music通道帧处理总开关
    short channels;
    short block_size;
    short bypass;
    short pre_soft_volume;
    short soft_volume;
    short fade_in_time_ms;
    short fade_out_time_ms;
    short fade_flag;        //1表示淡入，2表示淡出
    short output_channel_config;//1:左右声道交换 2: 单端耳机应用(L=R=原始的(L/2 + R/2)), 0 不做处理
                                //3:只输出左声道，4：只输出右声道
    short bit_width;
    short reserve_0;
}music_common_para_t;

typedef struct
{
    short freq_display_enable;
    short freq_point_num;
    short freq_point[10];
} freq_display_para_t;


typedef struct
{
    int sample_rate;
    int dae_buf_addr[2];     //帧处理buf，分左右声道，32bit位宽，长度分别是采样对数的4倍，传进来
   short dae_enable;        //mic通道帧处理总开关
    short channels;
    short bypass;
    short pre_soft_volume;
    short soft_volume;
    short fade_in_time_ms;
    short fade_out_time_ms;
    short fade_flag;        //1表示淡入，2表示淡出
    short bit_width;
    short output_channel_config;//1:左右声道交换 2: 单端耳机应用(L=R=原始的(L/2 + R/2)), 0 不做处理
                                //3:只输出左声道，4：只输出右声道
}mic_common_para_t;

typedef struct
{
    short enable;//是否打开限幅器，0表示不打开，1表示打开
	short limiter_version;
	short limiter_reserve0;//预留
	short limiter_reserve1;//预留
	short limiter_reserve2;//预留
    short Limiter_threshold;//[限幅器阈值，单位为0.1DB，范围 -60DB ~ 0DB; 
    short Limiter_attack_time;// 限幅器启动时间，单位为0.01ms，取值范围为 0.02 ~ 10 ms; 
    short Limiter_release_time;// 限幅器释放时间，单位为1ms，取值范围为 1 ~ 5000 ms]
}mic_limiter_para_t;


typedef struct
{
    short echo_enable;
    short echo_delay;
    short echo_decay;
    short echo_lowpass_fc1;
    short echo_lowpass_fc2;
    short decay_times; //for word aligned
}echo_para_t;

typedef struct
{
    short reverb_enable;
    short mRoomSize;                //房间大小(%):0~100                 默认：75
    short mPreDelay;                //预延迟(ms):0~100ms                默认：10
    short mReverberance;            //混响感(%):0~100                   默认：50
    short mHfDamping;               //高频衰减(%):0~100                 默认：50
    short mToneLow;                 //低切频率，32~523                  默认：32
    short mToneHigh;                //高切频率，523~8372Hz              默认：8372
    short mWetGain;                 //湿声(混响声)的增益调节，-60~10dB，默认-1dB
    short mDryGain;                 //干声(直达声)的增益调节，-60~10dB，默认-1dB
    short mStereoWidth;             //单声道此参数无意义，只支持单声道，设0即可
    short mWetOnly;                 //1表示只有湿声，默认为0
    short reserve_0;
}reverb_para_t;

//acoutic feedback cancellation(声反馈消除或啸叫抑制)
typedef struct
{
    /**********************************/
    //移频+自适应陷波器去啸叫
    short freq_shift_enable;
    short nhs_enable;
    short fft_th1;                          //啸叫检测门限1，默认16dB，精度1dB，现在为14dB
    short fft_th2;                          //啸叫检测门限2，默认33dB，精度1dB，现在为30dB
    short attack_time;                      //DRC启动时间，范围精度同limiter参数，现在为0.1ms，设为10
    short release_time;                     //DRC释放时间，范围精度同limiter参数，现在为500ms
    short DRC_dB;                               //DRC压缩的幅度，单位1dB，现为36dB
    short reserve_0;
}afc_para_t;

//频谱显示算法

typedef struct
{
    short pitch_shift_enable;
    short pitch_level;              //-12~12,0表示不变调，负表示降调，正表示升调
}pitch_shift_para_t;

typedef struct
{
    short voice_removal_enable;     //人声消除开关
    short resever_0;
}voice_removal_para_t;



typedef struct
{
    short Ducking_enable;
    short side_chain_threshold;
    unsigned short rms_detection_time;
    unsigned short attack_time;
    unsigned short release_time;
    short main_chain_target_gain;
} ducking_effect_para_out_t;

typedef struct
{
	short Surround_gain_l;//2				     //左声道强度， default: 1000 for 1.0, step, 0.001, 取值范围[-2000,2000] for [-2.0,2.0]
	short Surround_gain_r;//3	     	         //右声道强度， default: 1000 for 1.0, step, 0.001, 取值范围[-2000,2000] for [-2.0,2.0]
	short Surround_gain_ls;//4	     	         //左环绕强度， default: -1500 for -1.5, step, 0.001, 取值范围[-2000,2000] for [-2.0,2.0]
	short Surround_gain_rs;//5	     	         //右环绕强度， default: -1500 for -1.5, step, 0.001, 取值范围[-2000,2000] for [-2.0,2.0]
}v3d_sub_effect_t;

typedef struct
{
	short v3d_enable;
	short v3d_base_type;
	short v3d_total_types;
	int v3d_duration_ms;
	int v3d_cross_ms;
	v3d_sub_effect_t v3d_setting_info[8];
}v3d_effect_t;


/*for DAE AL*/
typedef struct
{
    music_common_para_t music_common_para;
    freq_display_para_t freq_display_para;

	char aset_bin[512];

#if 0
	char reserve[256];//for release header
#else	
	//karaok function here 180 bytes
    mic_common_para_t mic_common_para;
    mic_limiter_para_t mic_limiter_para;
    echo_para_t echo_para;
    reverb_para_t reverb_para;
    afc_para_t afc_para;

    pitch_shift_para_t pitch_shift_para;
    voice_removal_para_t voice_removal_para;

    
    ducking_effect_para_out_t ducking_effect_para;
    
    void *virtualbass_buf_pt;//need at least 812 bytes
    /****************** OK MIC BUFFER *****************/
    unsigned int echo_buf[6]; //每块Buffer 4K，最多24KB
    unsigned int freq_shift_buf; //0x590
    unsigned int pitch_shift_buf; //0xf00
    unsigned int reverb_buf1;
    unsigned int reverb_buf2;
    unsigned int reverb_buf3;
     unsigned int nhs_buf; //0x40f0
    /***********************************/
    short mic_mix_config_l; //按键音通道（如果是2个channels输入则用左声道）mix到播放左声道的方式，0表示不mix，1表示mix，2表示替换
    short mic_mix_config_r; //按键音通道（如果是2个channels输入则用右声道）mix到播放右声道的方式，0表示不mix，1表示mix，2表示替换
	short mic_peq_enable;
    short mic_peq_band_num; //mic通道PEQ点数，分配在数组后面；数组前面分配给播歌通道
#endif

	int dae_ext_enable;     //是否使能dae加强版， 0：关闭，1：开启
	int dae_ext_buf_addr;   //用于daee加强版的内存地址，0代表无足够内存，非零，代表可用内存地址
	int dae_ext_buf_len;    //用于dae加强版的内存长度，单位：字节，当前版本希望内存 >=11264 bytes

	v3d_effect_t v3d_effect_info;//增加V3D参数

    int share_ram_buf_len; //如果不足，会导致部分音效模块不起作用。
                           //高16bit，用于标识，固定为0xABCD,低16bit用于存放长度，比如1536,则这里填写(0xABCD0000 + 0x0600) = 0xABCD0600，标识不匹配也认为无效
    int share_ram_buf_addr;//share buf的地址,建议4字节对齐？

}as_dae_para_info_t;

#endif
