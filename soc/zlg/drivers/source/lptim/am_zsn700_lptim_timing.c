/*******************************************************************************
*                                 AMetal
*                       ----------------------------
*                       innovating embedded platform
*
* Copyright (c) 2001-2018 Guangzhou ZHIYUAN Electronics Co., Ltd.
* All rights reserved.
*
* Contact information:
* web site:    http://www.zlg.cn/
*******************************************************************************/

/**
 * \file
 * \brief   LPLPTIM定时功能驱动层实现
 *
 * \note
 *
 * \internal
 * \par Modification history
 * - 1.00 20-03-18  yrz, first implementation
 * \endinternal
 */
#include "am_int.h"
#include "am_clk.h"
#include "am_zsn700_lptim_timing.h"
#include "hw/amhw_zsn700_rcc.h"
#include "am_clk.h"
#include "zsn700_clk.h"

/* 共用同一中断号的设备数量 */
#define PUBLIC_INUM_DEV_NUM      2

/*******************************************************************************
  定时器驱动函数声明
*******************************************************************************/

static const am_timer_info_t * __lptim_timing_info_get (void *p_drv);

static int __lptim_timing_clkin_freq_get (void *, uint32_t *);
static int __lptim_timing_prescale_set (void *, uint8_t, uint32_t );
static int __lptim_timing_prescale_get (void *, uint8_t, uint32_t *);
static int __lptim_timing_count_get (void *, uint8_t, void *);
static int __lptim_timing_rollover_get (void *, uint8_t, void *);
static int __lptim_timing_disable (void *, uint8_t);
static int __lptim_timing_enable (void *, uint8_t, void *);
static int __lptim_timing_callback_set (void *, uint8_t, void (*)(void *), void *);

static void __lptim_irq_handler (void *p_arg);

/*******************************************************************************
  全局变量
*******************************************************************************/

/* 设备结构体指针 */
static
am_zsn700_lptim_timing_dev_t *__gp_dev[PUBLIC_INUM_DEV_NUM] = {NULL, NULL};

/* LPTIM定时器驱动函数 */
static const struct am_timer_drv_funcs __g_lptim_timing_drv_funcs = {
    __lptim_timing_info_get,
    __lptim_timing_clkin_freq_get,
    __lptim_timing_prescale_set,
    __lptim_timing_prescale_get,
    __lptim_timing_count_get,
    __lptim_timing_rollover_get,
    __lptim_timing_enable,
    __lptim_timing_disable,
    __lptim_timing_callback_set,
};

/*******************************************************************************
  中断处理
*******************************************************************************/

void __lptim_irq_handler (void *p_arg)
{
    amhw_zsn700_lptim_t *p_hw_tim = NULL;

    if(__gp_dev[0] != NULL) {

        p_hw_tim = (amhw_zsn700_lptim_t *)__gp_dev[0]->p_devinfo->lptim_regbase;
        if (amhw_zsn700_lptim_int_flag_check(p_hw_tim) == AM_TRUE ) {

           if (__gp_dev[0]->pfn_callback != NULL) {
               __gp_dev[0]->pfn_callback(__gp_dev[0]->p_arg);
           }

           /* 清除溢出标志 */
           amhw_zsn700_lptim_int_flag_clr(p_hw_tim);
       }
    }

    if(__gp_dev[1] != NULL) {

        p_hw_tim = (amhw_zsn700_lptim_t *)__gp_dev[1]->p_devinfo->lptim_regbase;
        if (amhw_zsn700_lptim_int_flag_check(p_hw_tim) == AM_TRUE ) {

           if (__gp_dev[1]->pfn_callback != NULL) {
               __gp_dev[1]->pfn_callback(__gp_dev[1]->p_arg);
           }

           /* 清除溢出标志 */
           amhw_zsn700_lptim_int_flag_clr(p_hw_tim);
       }
    }

}

/*******************************************************************************
  标准定时器驱动实现
*******************************************************************************/

static const am_timer_info_t * __lptim_timing_info_get (void *p_drv)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;

    if(p_dev->p_devinfo->lptim_regbase == ZSN700_LPTIM0_BASE) {
        return &(p_dev->timer_info);
    } else {
        return &(p_dev->timer_info);
    }
}

/******************************************************************************/
static int __lptim_timing_clkin_freq_get (void *p_drv, uint32_t *p_freq)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;

    if ((p_dev == NULL) || (p_freq == NULL)) {
        return -AM_EINVAL;
    }

    /* 获取当前系统时钟频率，systick 使用的是系统时钟 */
    *p_freq =  p_dev->clk_rate;

    return AM_OK;
}

/******************************************************************************/
static int __lptim_timing_prescale_set (void    *p_drv,
                                        uint8_t  chan,
                                        uint32_t prescale)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;
    amhw_zsn700_lptim_t          *p_hw_tim;

    if ((p_drv == NULL) || (chan != 0)) {
        return -AM_EINVAL;
    }

    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    if ((prescale != 0) && (prescale <= 256)) {

        if(prescale == 1) {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim, AMHW_ZSN700_LPTIM_CLK_DIV1);
        } else if(prescale == 2) {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim, AMHW_ZSN700_LPTIM_CLK_DIV2);
        } else if(prescale == 4) {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim, AMHW_ZSN700_LPTIM_CLK_DIV4);
        } else if(prescale == 8) {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim, AMHW_ZSN700_LPTIM_CLK_DIV8);
        } else if(prescale == 16) {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim,
                                          AMHW_ZSN700_LPTIM_CLK_DIV16);
        } else if(prescale == 32) {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim,
                                          AMHW_ZSN700_LPTIM_CLK_DIV32);
        } else if(prescale == 64) {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim,
                                          AMHW_ZSN700_LPTIM_CLK_DIV64);
        } else {
            amhw_zsn700_lptim_clk_div_set(p_hw_tim,
                                          AMHW_ZSN700_LPTIM_CLK_DIV256);
        }

        p_dev->timer_info.prescaler = prescale;

        return AM_OK;
    }

    return -AM_ENOTSUP;
}

/******************************************************************************/
static int __lptim_timing_prescale_get (void     *p_drv,
                                        uint8_t   chan,
                                        uint32_t *p_prescale)
{
    amhw_zsn700_lptim_clk_div_t   div   = AMHW_ZSN700_LPTIM_CLK_DIV1;
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;
    amhw_zsn700_lptim_t          *p_hw_tim;

    if ((p_dev == NULL) || (p_prescale == NULL) || (chan != 0)) {
        return -AM_EINVAL;
    }
 
    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    /* 获取分频值 */
    div = amhw_zsn700_lptim_clk_div_get(p_hw_tim);

    switch(div) {
    case AMHW_ZSN700_LPTIM_CLK_DIV1:
        *p_prescale = 1;
        break;
    case AMHW_ZSN700_LPTIM_CLK_DIV2:
        *p_prescale = 2;
        break;
    case AMHW_ZSN700_LPTIM_CLK_DIV4:
        *p_prescale = 4;
        break;
    case AMHW_ZSN700_LPTIM_CLK_DIV8:
        *p_prescale = 8;
        break;
    case AMHW_ZSN700_LPTIM_CLK_DIV16:
        *p_prescale = 16;
        break;
    case AMHW_ZSN700_LPTIM_CLK_DIV32:
        *p_prescale = 32;
        break;
    case AMHW_ZSN700_LPTIM_CLK_DIV64:
        *p_prescale = 64;
        break;
    case AMHW_ZSN700_LPTIM_CLK_DIV256:
        *p_prescale = 256;
        break;
    default:
        break;
    }

    return AM_OK;
}

/******************************************************************************/
static int __lptim_timing_count_get (void *p_drv, uint8_t chan, void *p_count)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;
    amhw_zsn700_lptim_t          *p_hw_tim;

    if ((p_dev == NULL) || (p_count == NULL) || (chan != 0)) {
        return -AM_EINVAL;
    }
 
    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    *(uint32_t *)p_count = amhw_zsn700_lptim_cnt16_count_get(p_hw_tim);

    return AM_OK;
}

/******************************************************************************/
static int __lptim_timing_rollover_get (void *p_drv,uint8_t chan,void *p_rollover)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;
    amhw_zsn700_lptim_t          *p_hw_tim;

    if ((p_dev == NULL) || (p_rollover == NULL) || (chan != 0)) {
        return -AM_EINVAL;
    }
 
    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    *(uint32_t *)p_rollover = 0xffff - amhw_zsn700_lptim_arr_count_get(p_hw_tim);

    return AM_OK;
}

/******************************************************************************/
static int __lptim_timing_disable (void *p_drv, uint8_t chan)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;
    amhw_zsn700_lptim_t          *p_hw_tim;
    
    if ((p_dev == NULL) || (chan != 0)) {
        return -AM_EINVAL;
    }
    
    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    /* 禁能中断 */
    amhw_zsn700_lptim_int_disable(p_hw_tim);

    /* 禁能定时器 */
    amhw_zsn700_lptim_disable(p_hw_tim);

    return AM_OK;
}

/******************************************************************************/
static int __lptim_timing_enable (void *p_drv, uint8_t chan, void *p_count)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;
    amhw_zsn700_lptim_t          *p_hw_tim;

    if ((p_dev == NULL) || (chan != 0) || (p_count == NULL)) {
        return -AM_EINVAL;
    }

    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    /* 禁止定时器产生中断 */
    amhw_zsn700_lptim_int_disable(p_hw_tim);

    /* 等待同步完成 */
    while(amhw_zsn700_lptim_write_into_arr_stat(p_hw_tim) == AM_FALSE);

    amhw_zsn700_lptim_arr_count_set(p_hw_tim, 65535 - (*(uint16_t *)p_count));

    /* 是否开启门控 */
    if(p_dev->p_devinfo->gate_enable == AM_TRUE) {
        amhw_zsn700_lptim_gate_enable(p_hw_tim);
    } else {
        amhw_zsn700_lptim_gate_disable(p_hw_tim);
    }

    /* 门控有效信号设置 */
    if(p_dev->p_devinfo->gate_state == AM_TRUE) {
        amhw_zsn700_lptim_gate_high(p_hw_tim);
    } else {
        amhw_zsn700_lptim_gate_low(p_hw_tim);
    }

    /* TOG、TOGN信号输出设置 */
    if(p_dev->p_devinfo->tog_state == AM_TRUE) {
        amhw_zsn700_lptim_tog_enable(p_hw_tim);
    } else {
        amhw_zsn700_lptim_tog_disable(p_hw_tim);
    }

    /* 清除标志位 */
    amhw_zsn700_lptim_int_flag_clr(p_hw_tim);

    if (p_dev->pfn_callback != NULL) {

        /* 中断使能 */
        amhw_zsn700_lptim_int_enable(p_hw_tim);
    }

    /* 使能定时器LPTIM允许计数 */
    amhw_zsn700_lptim_enable(p_hw_tim);

    return AM_OK;
}

/******************************************************************************/
static int __lptim_timing_callback_set (void     *p_drv,
                                        uint8_t   chan,
                                        void    (*pfn_callback)(void *),
                                        void     *p_arg)
{
    am_zsn700_lptim_timing_dev_t *p_dev = (am_zsn700_lptim_timing_dev_t *)p_drv;
    amhw_zsn700_lptim_t          *p_hw_tim;

    if ((p_dev == NULL) || (chan != 0)) {
        return -AM_EINVAL;
    }
 
    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    /* 不需要开启中断 */
    if (pfn_callback == NULL) {
        amhw_zsn700_lptim_disable(p_hw_tim);
    } else {
        p_dev->pfn_callback = pfn_callback;
        p_dev->p_arg        = p_arg;
    }

    return AM_OK;
}

/*******************************************************************************
  Public functions
*******************************************************************************/

am_timer_handle_t am_zsn700_lptim_timing_init (
    am_zsn700_lptim_timing_dev_t             *p_dev,
    const am_zsn700_lptim_timing_devinfo_t   *p_devinfo)
{
    uint8_t              i        = 0;
    am_bool_t            stat     = AM_FALSE;
    amhw_zsn700_lptim_t *p_hw_tim = NULL;

    if (NULL == p_dev || NULL == p_devinfo ) {
        return NULL;
    }

    if (p_devinfo->pfn_plfm_init) {
        p_devinfo->pfn_plfm_init();
    }

    p_dev->p_devinfo          = p_devinfo;
    p_hw_tim                  = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;
    p_dev->timer_serv.p_funcs = (struct am_timer_drv_funcs *)&__g_lptim_timing_drv_funcs;
    p_dev->timer_serv.p_drv   = p_dev;

    p_dev->pfn_callback = NULL;
    p_dev->p_arg        = NULL;

    /* 计数/计时功能选择 */
    amhw_zsn700_lptim_function_sel(p_hw_tim, p_devinfo->fun_sel);

    /* 判断是否中断使能函数已经连接到中断号上 */
    for(i = 0; i < PUBLIC_INUM_DEV_NUM; i++) {
        if(__gp_dev[i] != NULL) {
            stat = AM_TRUE;
            break;
        }
    }

    /* 还未连接中断号 */
    if(stat == AM_FALSE) {

        am_int_connect(p_dev->p_devinfo->inum, __lptim_irq_handler, NULL);
        am_int_enable(p_dev->p_devinfo->inum);
    }

    /* 判断基地址，分别获取设备结构体指针，以便中断回调函数调用不同设备回调函数 */
    if(p_dev->p_devinfo->lptim_regbase == ZSN700_LPTIM0_BASE) {
        __gp_dev[0] = p_dev;
    } else {
        __gp_dev[1] = p_dev;
    }

    /* 16位计数  ARR寄存器的值-0xFFFF计数 */
    p_dev->timer_info.counter_width = 16;
    p_dev->timer_info.prescaler     = 0xff;
    p_dev->timer_info.chan_num      = 1;

    if(p_devinfo->mode == AMHW_ZSN700_LPTIM_MODE_RELOAD) {

        amhw_zsn700_lptim_mode_sel(p_hw_tim, AMHW_ZSN700_LPTIM_MODE_RELOAD);

        p_dev->timer_info.features =
              AM_TIMER_CAN_INTERRUPT   |  /* 可以产生中断 */
              AM_TIMER_AUTO_RELOAD     |  /* 支持自动重载 */
              AM_TIMER_INTERMEDIATE_COUNT;/* 读取当前计数器的值   */

    } else {

        amhw_zsn700_lptim_mode_sel(p_hw_tim, AMHW_ZSN700_LPTIM_MODE_NORELOAD);

        p_dev->timer_info.features =
            AM_TIMER_CAN_INTERRUPT   |  /* 可以产生中断 */
            AM_TIMER_INTERMEDIATE_COUNT;/* 读取当前计数器的值   */
    }

    /* 时钟源设置 */
    amhw_zsn700_lptim_clk_src_sel(p_hw_tim, p_dev->p_devinfo->clk_src);

    if(p_dev->p_devinfo->clk_src == AMHW_ZSN700_LPTIM_CLK_SRC_XTL) {

        /* 外部晶振驱动能力选择 */
        amhw_zsn700_rcc_xtl_xtal_driver_set(AMHW_ZSN700_XTL_XTAL_DRIVER_DEFAULT);

        /* XTL晶振振荡幅度的调整 */
        amhw_zsn700_rcc_xtl_xtal_amplitude_set(
            AMHW_ZSN700_XTL_XTAL_AMPLITUDE_BIG_DEFAULT);

        /* 外部低速时钟XTL稳定时间选择 */
        amhw_zsn700_rcc_xtl_waittime_set(AMHW_ZSN700_XTL_WAITTIME_16384);

        amhw_zsn700_rcc_set_start(0x5A5A);
        amhw_zsn700_rcc_set_start(0xA5A5);

        amhw_zsn700_rcc_xtl_enable();

        /* 等待稳定*/
        while(amhw_zsn700_rcc_xtl_state_get() == AM_FALSE);

        p_dev->clk_rate = 32768;

    } else if(p_dev->p_devinfo->clk_src == AMHW_ZSN700_LPTIM_CLK_SRC_RCL) {

        /* 设置RCL时钟频率校准值 （38.4KHz）*/
        amhw_zsn700_rcc_rcl_trim_set(*((uint16_t*)AMHW_ZSN700_RCl_FRE_32768Hz));

        amhw_zsn700_rcc_rcl_waittime_set(AMHW_ZSN700_RCL_WAITTIME_256);

        amhw_zsn700_rcc_set_start(0x5A5A);
        amhw_zsn700_rcc_set_start(0xA5A5);

        amhw_zsn700_rcc_rcl_enable();

        /* 等待稳定*/
        while(amhw_zsn700_rcc_rcl_state_get() == AM_FALSE);

        p_dev->clk_rate = 32768;
    } else {
        p_dev->clk_rate = am_clk_rate_get(CLK_PCLK);
    }

    return &(p_dev->timer_serv);
}

/******************************************************************************/
void am_zsn700_lptim_timing_deinit (am_timer_handle_t handle)
{

    uint8_t                       i        = 0;
    am_bool_t                     stat     = AM_FALSE;

    am_zsn700_lptim_timing_dev_t *p_dev    = (am_zsn700_lptim_timing_dev_t *)handle;
    amhw_zsn700_lptim_t          *p_hw_tim = NULL;

    if (p_dev == NULL || p_dev->p_devinfo == NULL ) {
        return ;
    }

    p_hw_tim = (amhw_zsn700_lptim_t *)p_dev->p_devinfo->lptim_regbase;

    /* 关闭中断 */
    amhw_zsn700_lptim_int_disable(p_hw_tim);

    /* 关闭定时器 */
    amhw_zsn700_lptim_disable(p_hw_tim);

    /* 判断基地址，分别获取设备结构体指针，以便中断回调函数调用不同设备回调函数 */
    if(p_dev->p_devinfo->lptim_regbase == ZSN700_LPTIM0_BASE) {
        __gp_dev[0] = NULL;
    } else {
        __gp_dev[1] = NULL;
    }


    /* 判断是否所有设备都解初始化 */
    for(i = 0; i < PUBLIC_INUM_DEV_NUM; i++) {
        if(__gp_dev[i] != NULL) {
            stat = AM_TRUE;
            break;
        }
    }

    /* 已经全部解初始化，则删除中断号连接 */
    if(stat == AM_FALSE) {
        am_int_disconnect(p_dev->p_devinfo->inum, __lptim_irq_handler, (void *)p_dev);
        am_int_disable(p_dev->p_devinfo->inum);
    }

    p_dev->timer_serv.p_drv = NULL;

    if (p_dev->p_devinfo->pfn_plfm_deinit) {
        p_dev->p_devinfo->pfn_plfm_deinit();
    }
}

/* end of file */
