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
 * \brief SPI ������ʵ�ֺ���
 *
 * \internal
 * \par Modification history
 * - 1.01 15-11-20  sky, modified.
 * - 1.01 15-09-28  aii, second implementation.
 * - 1.00 15-07-07  aii, first implementation.
 * \endinternal
 */

/*******************************************************************************
includes
*******************************************************************************/

#include "ametal.h"
#include "am_int.h"
#include "am_gpio.h"
#include "am_kl26_spi_dma.h"
#include "am_kl26_dma.h"
#include "hw/amhw_fsl_spi.h"
#include "am_kl26_clk.h"
#include "../../../kl26/am_kl26.h"

/*******************************************************************************
  SPI ״̬���¼�����
*******************************************************************************/

/* ��ȡSPI������Ƶ�� */
#define __SPI_FRQIIN_GET(p_hw_spi)    am_kl26_clk_periph_rate_get(p_hw_spi)

/* ��ȡSPI��֧�ֵ�����ٶ� */
#define __SPI_MAXSPEED_GET(p_hw_spi) (__SPI_FRQIIN_GET(p_hw_spi) / 2)

/* ��ȡSPI֧�ֵ���С�ٶ� */
#define __SPI_MINSPEED_GET(p_hw_spi) (__SPI_FRQIIN_GET(p_hw_spi) / 4096)

/**
 * SPI ������״̬
 */

#define __SPI_ST_IDLE               0                   /* ����״̬ */
#define __SPI_ST_MSG_START          1                   /* ��Ϣ��ʼ */
#define __SPI_ST_TRANS_START        2                   /* ���俪ʼ */
#define __SPI_ST_DMA_TRANS_DATA     3                   /* DMA ���� */

/**
 * SPI �������¼�
 *
 * ��32λ����16λ���¼���ţ���16λ���¼�����
 */

#define __SPI_EVT_NUM_GET(event)    ((event) & 0xFFFF)
#define __SPI_EVT_PAR_GET(event)    ((event >> 16) & 0xFFFF)
#define __SPI_EVT(evt_num, evt_par) (((evt_num) & 0xFFFF) | ((evt_par) << 16))

#define __SPI_EVT_NONE              __SPI_EVT(0, 0)     /* ���¼� */
#define __SPI_EVT_TRANS_LAUNCH      __SPI_EVT(1, 0)     /* ������� */
#define __SPI_EVT_DMA_TRANS_DATA    __SPI_EVT(2, 0)     /* DMA�������� */

/*******************************************************************************
  ģ���ں�������
*******************************************************************************/
am_local void __spi_default_cs_ha    (am_spi_device_t *p_dev, int state);
am_local void __spi_default_cs_la    (am_spi_device_t *p_dev, int state);
am_local void __spi_default_cs_dummy (am_spi_device_t *p_dev, int state);

am_local void __spi_cs_on  (am_kl26_spi_dma_dev_t *p_this, am_spi_device_t *p_dev);
am_local void __spi_cs_off (am_kl26_spi_dma_dev_t *p_this, am_spi_device_t *p_dev);

am_local int  __spi_hard_init (am_kl26_spi_dma_dev_t *p_this);
am_local int  __spi_config (am_kl26_spi_dma_dev_t *p_this);

am_local uint32_t __spi_speed_cfg (am_kl26_spi_dma_dev_t *p_dev, uint32_t target_baud);

am_local int  __spi_mst_sm_event (am_kl26_spi_dma_dev_t *p_dev, uint32_t event);
/*******************************************************************************
  SPI������������
*******************************************************************************/
am_local int __spi_info_get (void *p_arg, am_spi_info_t   *p_info);
am_local int __spi_setup    (void *p_arg, am_spi_device_t *p_dev );
am_local int __spi_msg_start (void              *p_drv,
                              am_spi_device_t   *p_dev,
                              am_spi_message_t  *p_msg);

/**
 * \brief SPI ��������
 */
am_local am_const struct am_spi_drv_funcs __g_spi_drv_funcs = {
    __spi_info_get,
    __spi_setup,
    __spi_msg_start,
};

/******************************************************************************/

/**
 * \brief Ĭ��CS�ſ��ƺ������ߵ�ƽ��Ч
 */
am_local
void __spi_default_cs_ha (am_spi_device_t *p_dev, int state)
{
    am_gpio_set(p_dev->cs_pin, state ? 1 : 0);
}

/**
 * \brief Ĭ��CS�ſ��ƺ������͵�ƽ��Ч
 */
am_local
void __spi_default_cs_la (am_spi_device_t *p_dev, int state)
{
    am_gpio_set(p_dev->cs_pin, state ? 0 : 1);
}

/**
 * \brief Ĭ��CS�ſ��ƺ�������Ӳ�����п���
 */
am_local
void __spi_default_cs_dummy (am_spi_device_t *p_dev, int state)
{
    return;
}

/**
 * \brief CS���ż���
 */
am_local
void __spi_cs_on (am_kl26_spi_dma_dev_t *p_this, am_spi_device_t *p_dev)
{
    /* if last device toggled after message */
    if (p_this->p_tgl_dev != NULL) {

        /* last message on defferent device */
        if (p_this->p_tgl_dev != p_dev) {
            p_this->p_tgl_dev->pfunc_cs(p_this->p_tgl_dev, 0);
        }
        p_this->p_tgl_dev = NULL;
    }

    p_dev->pfunc_cs(p_dev, 1);
}

/**
 * \brief CS����ȥ��
 */
am_local
void __spi_cs_off (am_kl26_spi_dma_dev_t   *p_this,
                   am_spi_device_t  *p_dev)
{
    if (p_this->p_tgl_dev == p_dev) {
        p_this->p_tgl_dev = NULL;
    }

    p_dev->pfunc_cs(p_dev, 0);
}

/******************************************************************************/

/**
 * \brief ����һ�� message �������������б�ĩβ
 * \attention ���ô˺�����������������
 */
am_static_inline
void __spi_msg_in (am_kl26_spi_dma_dev_t *p_dev, struct am_spi_message *p_msg)
{
    am_list_add_tail((struct am_list_head *)(&p_msg->ctlrdata),
                                             &(p_dev->msg_list));
}

/**
 * \brief �ӿ����������б���ͷȡ��һ�� message
 * \attention ���ô˺�����������������
 */
am_static_inline
struct am_spi_message *__spi_msg_out (am_kl26_spi_dma_dev_t *p_dev)
{
    if (am_list_empty_careful(&(p_dev->msg_list))) {
        return NULL;
    } else {
        struct am_list_head *p_node = p_dev->msg_list.next;
        am_list_del(p_node);
        return am_list_entry(p_node, struct am_spi_message, ctlrdata);
    }
}

/**
 * \brief ��message�б���ͷȡ��һ�� transfer
 * \attention ���ô˺�����������������
 */
am_static_inline
struct am_spi_transfer *__spi_trans_out (am_spi_message_t *msg)
{
    if (am_list_empty_careful(&(msg->transfers))) {
        return NULL;
    } else {
        struct am_list_head *p_node = msg->transfers.next;
        am_list_del(p_node);
        return am_list_entry(p_node, struct am_spi_transfer, trans_node);
    }
}

/******************************************************************************/
am_local
int __spi_setup (void *p_arg, am_spi_device_t *p_dev)
{
    am_kl26_spi_dma_dev_t *p_this   = (am_kl26_spi_dma_dev_t *)p_arg;
    amhw_fsl_spi_t   *p_hw_spi = (amhw_fsl_spi_t *)(p_this->p_devinfo->spi_regbase);

    uint32_t max_speed, min_speed;

    if (p_dev == NULL) {
        return -AM_EINVAL;
    }

    /* Ĭ������Ϊ8λ����󲻳���16λ */
    if (p_dev->bits_per_word == 0) {
        p_dev->bits_per_word = 8;
    } else if (p_dev->bits_per_word > 16) {
        return -AM_ENOTSUP;
    }

    /* ���SPI���ʲ��ܳ�����ʱ�ӣ���С����С����ʱ��65536��Ƶ */
    max_speed = __SPI_MAXSPEED_GET(p_hw_spi);
    min_speed = __SPI_MINSPEED_GET(p_hw_spi);

    if (p_dev->max_speed_hz > max_speed) {
        p_dev->max_speed_hz = max_speed;
    } else if (p_dev->max_speed_hz < min_speed) {
        return -AM_ENOTSUP;
    }

    /* ��Ƭѡ���� */
    if (p_dev->mode & AM_SPI_NO_CS) {
        p_dev->pfunc_cs = __spi_default_cs_dummy;

    /* ��Ƭѡ���� */
    }  else {

        /* ���ṩ��Ĭ��Ƭѡ���� */
        if (p_dev->pfunc_cs == NULL) {

            /* Ƭѡ�ߵ�ƽ��Ч */
            if (p_dev->mode & AM_SPI_CS_HIGH) {
                am_gpio_pin_cfg(p_dev->cs_pin, AM_GPIO_OUTPUT_INIT_LOW);
                p_dev->pfunc_cs = __spi_default_cs_ha;

            /* Ƭѡ�͵�ƽ��Ч */
            } else {
                am_gpio_pin_cfg(p_dev->cs_pin, AM_GPIO_OUTPUT_INIT_HIGH);
                p_dev->pfunc_cs = __spi_default_cs_la;
            }
        }
    }

    /* ���Ƭѡ�ź� */
    __spi_cs_off(p_this, p_dev);

    return AM_OK;
}


am_local
int __spi_info_get (void *p_arg, am_spi_info_t *p_info)
{
    am_kl26_spi_dma_dev_t  *p_this   = (am_kl26_spi_dma_dev_t *)p_arg;
    amhw_fsl_spi_t    *p_hw_spi = (amhw_fsl_spi_t *)(p_this->p_devinfo->spi_regbase);

    if (p_info == NULL) {
        return -AM_EINVAL;
    }

    /* ������ʵ��� PCLK */
    p_info->max_speed = __SPI_MAXSPEED_GET(p_hw_spi);
    p_info->min_speed = __SPI_MINSPEED_GET(p_hw_spi);
    p_info->features  = AM_SPI_CS_HIGH   |
                        AM_SPI_LSB_FIRST |
                        AM_SPI_MODE_0    |
                        AM_SPI_MODE_1    |
                        AM_SPI_MODE_2    |
                        AM_SPI_MODE_3;   /* features */
    return AM_OK;
}

/**
 * \brief SPI Ӳ����ʼ��
 */
am_local
int __spi_hard_init (am_kl26_spi_dma_dev_t *p_this)
{
    amhw_fsl_spi_t *p_hw_spi = (amhw_fsl_spi_t *)(p_this->p_devinfo->spi_regbase);

    if (p_this == NULL) {
        return -AM_EINVAL;
    }

    /* ����Ϊ����ģʽ */
    amhw_fsl_spi_workmode_cfg(p_hw_spi, AMHW_FSL_SPI_WMODE_MASTER);

    amhw_fsl_spi_enable(p_hw_spi);

    /* ��ʼ������SPI */
    return AM_OK;
}


am_local
void __dma_isr (void *p_arg, uint8_t stat)
{
    am_kl26_spi_dma_dev_t  *p_this   = (am_kl26_spi_dma_dev_t *)p_arg;
    amhw_fsl_spi_t    *p_hw_spi = (amhw_fsl_spi_t *)(p_this->p_devinfo->spi_regbase);

    /* ������ж�A���� */
    if (stat == AM_KL26_DMA_INT_NORMAL)
    {
        /* ��¼�ɹ������ֽ��� */
        if (p_this->p_cur_trans->p_txbuf != NULL) {
            p_this->p_cur_msg->actual_length += (p_this->p_cur_trans->nbytes)
                            *(p_this->p_cur_spi_dev->bits_per_word < 9 ? 1 : 2);
        }

        /* �ȴ�SPI���ݳ��׷��ͽ��� */
        while(!(amhw_fsl_spi_stat_get(p_hw_spi) & AMHW_FSL_SPI_STAT_T_EMPTY));
        /* ������� */
        __spi_mst_sm_event(p_this, __SPI_EVT_TRANS_LAUNCH);

    } else {
        /* �ж�Դ��ƥ�� */
    }
}

/**
 * \brief  SPI ʹ��DMA����
 */
am_local
int __spi_dma_trans (am_kl26_spi_dma_dev_t *p_dev)
{
    am_kl26_spi_dma_dev_t           *p_this      = (am_kl26_spi_dma_dev_t *)p_dev;
    const am_kl26_spi_dma_devinfo_t *p_devinfo   = p_this->p_devinfo;
    amhw_fsl_spi_t                 *p_hw_spi    = (amhw_fsl_spi_t *)(p_devinfo->spi_regbase);
    am_spi_transfer_t               *p_cur_trans = p_dev->p_cur_trans;

    const void *p_txbuf = NULL;

    uint8_t nbytes_per_word = p_dev->p_cur_spi_dev->bits_per_word < 9 ? 1 : 2;

    static uint16_t tx_rx_trans = 0;

    uint32_t   dma_flags[3] = {0};  /* DMA����ͨ�������� */

    /* ����DMA�жϷ����� */
    am_kl26_dma_isr_connect(p_this->p_devinfo->dma_chan_rx, __dma_isr, (void *)p_this);



    /* DMA����ͨ������ */
    dma_flags[0] = KL26_DMA_DCR_INTERRUTP_DISABLE        |  /* DMA�жϽ���            */
                   KL26_DMA_DCR_PER_REQUEST_ENABLE       |  /* ��������ʹ��           */
                   KL26_DMA_DCR_SINGLE_TRANSFERS         |  /* ���δ���               */
                   KL26_DMA_DCR_DESTINATION_INCR_DISABLE |  /* Ŀ�ĵ�ַ��������      */
                   KL26_DMA_DCR_REQUEST_AFFECTED         |  /* ������Ӱ��             */
                   KL26_DMA_DCR_START_DISABLE    ;           /* DMA��������        */

    /* DMA����ͨ������ */
    dma_flags[1] = KL26_DMA_DCR_INTERRUTP_ENABLE         |  /* DMA�ж�ʹ��            */
                   KL26_DMA_DCR_PER_REQUEST_ENABLE       |  /* ��������ʹ��           */
                   KL26_DMA_DCR_SINGLE_TRANSFERS         |  /* ���δ���               */
                   KL26_DMA_DCR_SOURCE_INCR_DISABLE      |  /* Դ��ַ��������        */
                   KL26_DMA_DCR_REQUEST_AFFECTED         |  /* ������Ӱ��             */
                   KL26_DMA_DCR_START_DISABLE;               /* DMA��������        */

    /* ����DMA����λ�� */
    if (p_cur_trans->bits_per_word > 8) {
        dma_flags[0] |= KL26_DMA_DCR_SOURCE_SIZE_16_BIT;
        dma_flags[0] |= KL26_DMA_DCR_DESTINATION_SIZE_16_BIT;
        dma_flags[1] |= KL26_DMA_DCR_SOURCE_SIZE_16_BIT;
        dma_flags[1] |= KL26_DMA_DCR_DESTINATION_SIZE_16_BIT;
    } else {
        dma_flags[0] |= KL26_DMA_DCR_SOURCE_SIZE_8_BIT;
        dma_flags[0] |= KL26_DMA_DCR_DESTINATION_SIZE_8_BIT;
        dma_flags[1] |= KL26_DMA_DCR_SOURCE_SIZE_8_BIT;
        dma_flags[1] |= KL26_DMA_DCR_DESTINATION_SIZE_8_BIT;
    }

    /* ֻ���Ͳ��������� */
    if (p_cur_trans->p_rxbuf == NULL) {
        dma_flags[1] |= KL26_DMA_DCR_DESTINATION_INCR_DISABLE;

        /* ��������ͨ�������� */
        am_kl26_dma_xfer_desc_build(&(p_this->g_desc[1]),            /* ͨ��������       */
                                    (uint32_t)(&(p_hw_spi->dl)),     /* Դ�������׵�ַ   */
                                    (uint32_t)(&tx_rx_trans),        /* Ŀ�Ļ������׵�ַ */
                                    p_cur_trans->nbytes,             /* �����ֽ���       */
                                    dma_flags[1]);                   /* ��������         */
    /* ���ڽ������� */
    } else {
        dma_flags[1] |= KL26_DMA_DCR_DESTINATION_INCR_ENABLE;

        /* ��������ͨ�������� */
        am_kl26_dma_xfer_desc_build(&(p_this->g_desc[1]),            /* ͨ��������       */
                                    (uint32_t)(&(p_hw_spi->dl)),     /* Դ�������׵�ַ   */
                                    (uint32_t)(p_cur_trans->p_rxbuf),/* Ŀ�Ļ������׵�ַ */
                                    p_cur_trans->nbytes,             /* �����ֽ���       */
                                    dma_flags[1]);                   /* ��������         */
    }

    if (p_cur_trans->p_txbuf == NULL) {
        tx_rx_trans = 0;
        p_txbuf = &tx_rx_trans;
        dma_flags[0] |= KL26_DMA_DCR_SOURCE_INCR_DISABLE;

        /* ��������ͨ�������� */
        am_kl26_dma_xfer_desc_build(&(p_this->g_desc[0]),                  /* ͨ��������       */
                                    (uint32_t)(p_txbuf),                   /* Դ�������׵�ַ   */
                                    (uint32_t)(&(p_hw_spi->dl)),           /* Ŀ�Ļ������׵�ַ */
                                    p_cur_trans->nbytes - nbytes_per_word, /* �����ֽ���       */
                                    dma_flags[0]);                         /* ��������         */
    } else {
        p_txbuf = p_cur_trans->p_txbuf;
        dma_flags[0] |= KL26_DMA_DCR_SOURCE_INCR_ENABLE;

        /* ��������ͨ�������� */
        am_kl26_dma_xfer_desc_build(&(p_this->g_desc[0]),                  /* ͨ��������       */
                                    (uint32_t)((uint8_t *)p_txbuf + nbytes_per_word),    /* Դ�������׵�ַ   */
                                    (uint32_t)(&(p_hw_spi->dl)),           /* Ŀ�Ļ������׵�ַ */
                                    p_cur_trans->nbytes - nbytes_per_word, /* �����ֽ���       */
                                    dma_flags[0]);                         /* ��������         */
    }

    /* CSѡͨ */
    __spi_cs_on(p_this, p_this->p_cur_spi_dev);

    amhw_kl26_dma_status_clear (KL26_DMA, p_devinfo->dma_chan_rx);
    amhw_kl26_dma_status_clear (KL26_DMA, p_devinfo->dma_chan_tx);

    amhw_kl26_dma_chan_disable (KL26_DMAMUX, p_devinfo->dma_chan_rx);
    amhw_kl26_dma_chan_enable (KL26_DMAMUX, p_devinfo->dma_chan_rx);

    amhw_kl26_dma_chan_disable (KL26_DMAMUX, p_devinfo->dma_chan_tx);
    amhw_kl26_dma_chan_enable (KL26_DMAMUX, p_devinfo->dma_chan_tx);

    /* ����DMA���䣬���Ͽ�ʼ����(DMA���䣻����) */
    am_kl26_dma_chan_start(&p_this->g_desc[1], KL26_DMA_PER_TO_PER, p_this->p_devinfo->dma_chan_rx);

    amhw_fsl_spi_stat_get (p_hw_spi);
    if (nbytes_per_word == 1) {
        amhw_fsl_spi_data8_wirte (p_hw_spi, ((uint8_t *)p_txbuf)[0]);
    } else {
        amhw_fsl_spi_data16_wirte (p_hw_spi, ((uint16_t *)p_txbuf)[0]);
    }
    if (p_cur_trans->nbytes > nbytes_per_word) {
        /* ����DMA���䣬���Ͽ�ʼ���� */
        am_kl26_dma_chan_start(&p_this->g_desc[0], KL26_DMA_PER_TO_PER, p_this->p_devinfo->dma_chan_tx);
        amhw_fsl_spi_feature_cfg (p_hw_spi, AMHW_FSL_SPI_CFG_TDMA_EN);
    }

    amhw_fsl_spi_feature_cfg (p_hw_spi, AMHW_FSL_SPI_CFG_RDMA_EN);



    return AM_OK;
}

am_local
int __spi_config (am_kl26_spi_dma_dev_t *p_this)
{
    const am_kl26_spi_dma_devinfo_t *p_devinfo = p_this->p_devinfo;
    amhw_fsl_spi_t             *p_hw_spi  = (amhw_fsl_spi_t *)(p_devinfo->spi_regbase);
    am_spi_transfer_t           *p_trans   = p_this->p_cur_trans;

    uint32_t                       mode_flag = 0;

    /* ���Ϊ0��ʹ��Ĭ�ϲ���ֵ */
    if (p_trans->bits_per_word == 0) {
        p_trans->bits_per_word = p_this->p_cur_spi_dev->bits_per_word;
    }

    if (p_trans->speed_hz == 0) {
        p_trans->speed_hz = p_this->p_cur_spi_dev->max_speed_hz;
    }

    /* �����ֽ�����Ч�Լ�� */
    if (p_trans->bits_per_word > 16 || p_trans->bits_per_word < 1) {
        return -AM_EINVAL;
    }

    /* ���÷�Ƶֵ��Ч�Լ�� */
    if (p_trans->speed_hz > __SPI_MAXSPEED_GET(p_hw_spi) ||
        p_trans->speed_hz < __SPI_MINSPEED_GET(p_hw_spi)) {
        return -AM_EINVAL;
    }

    /* ���ͺͽ��ջ�������Ч�Լ�� */
    if ((p_trans->p_txbuf == NULL) && (p_trans->p_rxbuf == NULL)) {
        return -AM_EINVAL;
    }

    /* �����ֽ������ */
    if (p_trans->nbytes == 0) {
        return -AM_ELOW;
    }

    /**
     * ���õ�ǰ�豸ģʽ
     */
    mode_flag = 0;

    if (p_this->p_cur_spi_dev->mode & AM_SPI_CS_HIGH) {
        mode_flag |= AMHW_FSL_SPI_CS_SPI_OUT;
    }
    if (p_this->p_cur_spi_dev->mode & AM_SPI_LSB_FIRST) {
        mode_flag |= AMHW_FSL_SPI_CFG_LSB;
    }

    switch ( p_this->p_cur_spi_dev->mode & 0X03 ) {
    case AM_SPI_MODE_0 :
        mode_flag |= AMHW_FSL_SPI_MODE_0;
        break;
    case AM_SPI_MODE_1 :
        mode_flag |= AMHW_FSL_SPI_MODE_1;
        break;
    case AM_SPI_MODE_2 :
        mode_flag |= AMHW_FSL_SPI_MODE_2;
        break;
    case AM_SPI_MODE_3 :
        mode_flag |= AMHW_FSL_SPI_MODE_3;
        break;
    default:
        break;
    }

    /* ����SPIģʽ��ʱ����λ�ͼ��ԣ� */
    amhw_fsl_spi_mode_cfg(p_hw_spi, mode_flag);

    /* ����SPI�������� */
    amhw_fsl_spi_feature_cfg(p_hw_spi,  p_devinfo->flags |
                                         mode_flag);

    /* ����SPIƬѡ����CS���� */
    amhw_fsl_spi_cs_cfg(p_hw_spi, AMHW_FSL_SPI_CS_GPIO);

    /* ����Ϊ����ģʽ */
    amhw_fsl_spi_workmode_cfg(p_hw_spi, AMHW_FSL_SPI_WMODE_MASTER);

    /* ����SPI���� */
    __spi_speed_cfg(p_this, p_trans->speed_hz);

    return AM_OK;
}

/**
 * \brief SPI �������ݺ���
 */
am_local
int __spi_msg_start (void              *p_drv,
                     am_spi_device_t   *p_dev,
                     am_spi_message_t  *p_msg)
{
    am_kl26_spi_dma_dev_t *p_this   = (am_kl26_spi_dma_dev_t *)p_drv;

    int key;

    p_this->p_cur_spi_dev  = p_dev;                         /* ����ǰ�豸������Ϣ���� */
    p_this->p_cur_msg      = p_msg;                         /* ����ǰ�豸������Ϣ���� */
    p_this->nbytes_to_recv = 0;                             /* �������ַ�����0 */
    p_this->data_ptr       = 0;                             /* �ѽ����ַ�����0 */

    /* �豸��Ч�Լ�� */
    if ((p_drv              == NULL) ||
        (p_dev              == NULL) ||
        (p_msg              == NULL) ){
        return -AM_EINVAL;
    }


    key = am_int_cpu_lock();

    /* ��ǰ���ڴ�����Ϣ��ֻ��Ҫ���µ���Ϣ������������ */
    if (p_this->busy == AM_TRUE) {
        __spi_msg_in(p_this, p_msg);
        am_int_cpu_unlock(key);
        return AM_OK;
    } else {
        p_this->busy = AM_TRUE;
        __spi_msg_in(p_this, p_msg);
        p_msg->status = -AM_EISCONN; /* �����Ŷ��� */
        am_int_cpu_unlock(key);

        /* ����״̬�� */
        return __spi_mst_sm_event(p_this, __SPI_EVT_TRANS_LAUNCH);
    }
}

/******************************************************************************/

/*  ״̬���ڲ�״̬�л� */
#define __SPI_NEXT_STATE(s, e) \
    do { \
        p_dev->state = (s); \
        new_event = (e); \
    } while(0)

/**
 * \brief  SPI ʹ��״̬������
 */
am_local
int __spi_mst_sm_event (am_kl26_spi_dma_dev_t *p_dev, uint32_t event)
{
    amhw_fsl_spi_t *p_hw_spi = (amhw_fsl_spi_t *)(p_dev->p_devinfo->spi_regbase);

    volatile uint32_t new_event = __SPI_EVT_NONE;

    if (p_dev == NULL) {
        return -AM_EINVAL;
    }

    while (1) {

        if (new_event != __SPI_EVT_NONE) {     /* ������¼��Ƿ������ڲ� */
            event     = new_event;
            new_event  = __SPI_EVT_NONE;
        }

        switch (p_dev->state) {

        case __SPI_ST_IDLE:         /* ���������ڿ���״̬ */
        {
            if (event != __SPI_EVT_TRANS_LAUNCH) {
                return -AM_EINVAL;  /* ����״̬�ȴ�����Ϣ�������������� */
            }
            /* �л�����Ϣ��ʼ״̬������break */
        }
        /* no break */

        case __SPI_ST_MSG_START:    /* ��Ϣ��ʼ */
        {
            am_spi_message_t  *p_cur_msg   = NULL;

            int key;

            if (event != __SPI_EVT_TRANS_LAUNCH) {
                return -AM_EINVAL;  /* ��Ϣ��ʼ״̬�ȴ�����Ϣ�������������� */
            }

            key = am_int_cpu_lock();
            p_cur_msg          = __spi_msg_out(p_dev);
            p_dev->p_cur_msg   = p_cur_msg;

            if (p_cur_msg) {
                p_cur_msg->status = -AM_EINPROGRESS;
            } else {
                /* ���������ж� */
                amhw_fsl_spi_int_disable(p_hw_spi, AMHW_FSL_SPI_IRQ_R_MODDEF |
                                                    AMHW_FSL_SPI_IRQ_T);
                p_dev->busy = AM_FALSE;
            }
            am_int_cpu_unlock(key);

            /* ����Ҫ��������Ϣ */
            if (p_cur_msg == NULL) {
                __SPI_NEXT_STATE(__SPI_ST_IDLE, __SPI_EVT_NONE);
                break;
            } else {

                /* ֱ�ӽ�����һ��״̬����ʼһ�����䣬�˴�����break */
                __SPI_NEXT_STATE(__SPI_ST_TRANS_START, __SPI_EVT_TRANS_LAUNCH);

                event     = new_event;
                new_event = __SPI_EVT_NONE;
            }
        }
        /* no break */

        case __SPI_ST_TRANS_START:  /* ���俪ʼ */
        {
            am_spi_message_t  *p_cur_msg   = p_dev->p_cur_msg;

            if (event != __SPI_EVT_TRANS_LAUNCH) {
                return -AM_EINVAL;  /* ���俪ʼ״̬�ȴ�����Ϣ�������������� */
            }

            /* ��ǰ��Ϣ������� */
            if (am_list_empty(&(p_cur_msg->transfers))) {

                p_cur_msg->actual_length = 0;

                /* ��Ϣ���ڴ����� */
                if (p_cur_msg->status == -AM_EINPROGRESS) {
                    p_cur_msg->status = AM_OK;
                }

                /* notify the caller  */
                if (p_cur_msg->pfn_complete != NULL) {
                    p_cur_msg->pfn_complete(p_cur_msg->p_arg);
                }

                /* Ƭѡ�ر� */
                __spi_cs_off(p_dev, p_dev->p_cur_spi_dev);

                __SPI_NEXT_STATE(__SPI_ST_MSG_START, __SPI_EVT_TRANS_LAUNCH);

            } else {
                /* ��ȡ��һ�����䣬��ȷ�����ô��伴�� */
                am_spi_transfer_t *p_cur_trans = __spi_trans_out(p_cur_msg);
                p_dev->p_cur_trans             = p_cur_trans;

                /* reset current data pointer */
                p_dev->data_ptr       = 0;
                p_dev->nbytes_to_recv = 0;

                /* ����SPI������� */
                __spi_config(p_dev);

                /* �ж�ʹ�ú��ִ��䷽ʽ */
                amhw_fsl_spi_feature_cfg(p_hw_spi,
                                    (p_dev->p_cur_spi_dev->bits_per_word > 9) ? AMHW_FSL_SPI_CFG_16BIT_WIDTH :
                                                                                AMHW_FSL_SPI_CFG_8BIT_WIDTH);
//                amhw_fsl_spi_cs_cfg (p_hw_spi, 0x12);
//                amhw_fsl_spi_feature_cfg (p_hw_spi, AMHW_FSL_SPI_CFG_RDMA_EN | AMHW_FSL_SPI_CFG_TDMA_EN);

                __SPI_NEXT_STATE(__SPI_ST_DMA_TRANS_DATA, __SPI_EVT_DMA_TRANS_DATA);

            }
            break;
        }
        case __SPI_ST_DMA_TRANS_DATA:    /* DMA�������� */
        {
            if (event != __SPI_EVT_DMA_TRANS_DATA) {
                return -AM_EINVAL;  /* ��������״̬�ȴ�����Ϣ�����Ƿ������� */
            }

            /* ��һ״̬���Ƿ���״̬ */
            __SPI_NEXT_STATE(__SPI_ST_TRANS_START, __SPI_EVT_NONE);

            /* ʹ��DMA���� */
            __spi_dma_trans(p_dev);

            break;
        }

        /*
         * ��ԶҲ�������е����
         */
        default:
            break;
        }

        /* û�������ڲ�����Ϣ, ���� */
        if (new_event == __SPI_EVT_NONE) {
            break;
        }
    }
    return AM_OK;
}

/******************************************************************************/

/**
 * \brief SPI ��ʼ��
 */
am_spi_handle_t am_kl26_spi_dma_init (am_kl26_spi_dma_dev_t           *p_dev,
                                      const am_kl26_spi_dma_devinfo_t *p_devinfo)
{
    if (NULL == p_devinfo || NULL == p_dev ) {
        return NULL;
    }

    if (p_devinfo->pfn_plfm_init) {
        p_devinfo->pfn_plfm_init();
    }

    p_dev->spi_serve.p_funcs = (struct am_spi_drv_funcs *)&__g_spi_drv_funcs;
    p_dev->spi_serve.p_drv   = p_dev;

    p_dev->p_devinfo = p_devinfo;

    p_dev->p_cur_spi_dev    = NULL;
    p_dev->p_tgl_dev        = NULL;
    p_dev->busy             = AM_FALSE;
    p_dev->p_cur_msg        = NULL;
    p_dev->p_cur_trans      = NULL;
    p_dev->data_ptr         = 0;
    p_dev->nbytes_to_recv   = 0;
    p_dev->state            = __SPI_ST_IDLE;     /* ��ʼ��Ϊ����״̬ */
    p_dev->tx_rx_trash      = 0x55;

    am_list_head_init(&(p_dev->msg_list));

    if (__spi_hard_init(p_dev) != AM_OK) {
        return NULL;
    }


    am_kl26_dma_chan_cfg(p_devinfo->dma_chan_rx,
                         KL26_DMA_TRIGGER_DISABLE |  /**< \brief DMA����ģʽ   */
                         p_devinfo->dma_src_rx);          /**< \brief ����Դ        */

    am_kl26_dma_chan_cfg(p_devinfo->dma_chan_tx,
                         KL26_DMA_TRIGGER_DISABLE |  /**< \brief DMA����ģʽ   */
                         p_devinfo->dma_src_tx);          /**< \brief ����Դ        */

    amhw_kl26_dma_chan_enable (KL26_DMAMUX, p_devinfo->dma_chan_rx);
    amhw_kl26_dma_chan_enable (KL26_DMAMUX, p_devinfo->dma_chan_tx);

    return &(p_dev->spi_serve);
}

/**
 * \brief SPI ȥ����ʼ��
 */
void am_kl26_spi_dma_deinit (am_spi_handle_t handle)
{
    am_kl26_spi_dma_dev_t *p_dev    = (am_kl26_spi_dma_dev_t *)handle;
    amhw_fsl_spi_t   *p_hw_spi = (amhw_fsl_spi_t *)(p_dev->p_devinfo->spi_regbase);

    if (NULL == p_dev) {
        return ;
    }

    p_dev->spi_serve.p_funcs = NULL;
    p_dev->spi_serve.p_drv   = NULL;

    /* ���� SPI */
    amhw_fsl_spi_disable(p_hw_spi);


    if (p_dev->p_devinfo->pfn_plfm_deinit) {
        p_dev->p_devinfo->pfn_plfm_deinit();
    }
}


/**
 * \brief SPI�����ٶ�����
 *
 * \param[in]  p_dev        : ָ��SPI�豸��ָ��
 * \param[in]  target_speed : Ŀ���ٶ�
 *
 * \retval 0 ����֧��Ŀ���ٶ�,û�����óɹ�
 * \return �������õ��ٶȣ�������Ŀ���ٶȲ�ͬ
 *
 * \note   ���óɹ��������SPI�豸�е�cur_speed��min_diff
 */
am_local
uint32_t __spi_speed_cfg (am_kl26_spi_dma_dev_t *p_dev, uint32_t target_speed)
{

    uint32_t real_speed, best_speed;  /* ��������ٶ�    */
    uint32_t pdiv, best_pdiv;         /* Ԥ��Ƶֵ        */
    uint32_t div, best_div;           /* ����ֵ          */
    uint32_t diff, min_diff;          /* �ٶȲ�ֵ        */
    uint32_t max_speed, min_speed;    /* ֧���ٶȷ�Χ    */
    uint32_t spi_freq;                /* SPI����ʱ��Ƶ�� */

    amhw_fsl_spi_t *p_hw_spi = (amhw_fsl_spi_t *)(p_dev->p_devinfo->spi_regbase);

    spi_freq  = __SPI_FRQIIN_GET(p_hw_spi);
    max_speed = __SPI_MAXSPEED_GET(p_hw_spi);
    min_speed = __SPI_MINSPEED_GET(p_hw_spi);

    if (target_speed > max_speed || target_speed < min_speed) {
        return 0;
    }

   min_diff = 0xFFFFFFFFU;

   /* ����Ϊ���ķ�Ƶֵ,�ٶ�Ϊ��С */
   best_pdiv  = 7;
   best_div   = 8;
   best_speed = min_speed;

   /* ��������ʵ��ٶ� */
   for (pdiv = 0; (pdiv <= 7) && min_diff; pdiv++)
   {
       for (div = 0; (div <= 8) && min_diff; div++)
       {
           /* all_div = (pdiv+1) * 2^(div+1) */
           real_speed = (spi_freq / ((pdiv + 1) << (div+1)));

           /* ʵ���ٶ�ӦС��Ŀ���ٶ� */
           if (target_speed >= real_speed)
           {
               diff = target_speed-real_speed;

               if (min_diff > diff)
               {
                   /* ����ʵ��ٶ� */
                   min_diff   = diff;
                   best_pdiv  = pdiv;
                   best_div   = div;
                   best_speed = real_speed;
               }
           }
       }
   }

   amhw_fsl_spi_div_cfg(p_hw_spi, best_pdiv, best_div);

   return best_speed;
}
