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
 * \brief MiniPort-ZLG72128 ���̣�ͨ����׼�ӿ�ʵ��
 *
 * - ��������
 *   1. �� MiniPort-ZLG72128 ������ HC32 �����ӡ�
 *
 * - ʵ������
 *   1. ����һ����������λ�������ʾ������ŵĶ����ƣ�
 *   2. ż�����ʱ���������˸���������ʱ������ܲ���˸��
 *
 * \note
 *    ���Ա� Demo ������ am_prj_config.h �ڽ� AM_CFG_KEY_GPIO_ENABLE��
 *    AM_CFG_KEY_ENABLE �� AM_CFG_SOFTIMER_ENABLE ����Ϊ 1������Щ��
 *    �Ѿ�Ĭ������Ϊ 1�� �û������ٴ����á�
 *
 * \par Դ����
 * \snippet demo_miniport_zlg72128.c src_miniport_zlg72128
 *
 * \internal
 * \par Modification history
 * - 1.00 19-09-27
 * \endinternal
 */

/**
 * \addtogroup demo_if_miniport_zlg72128
 * \copydoc demo_miniport_zlg72128.c
 */

/** [src_miniport_zlg72128] */
#include "ametal.h"
#include "am_vdebug.h"
#include "am_hc32l13x_inst_init.h"
#include "demo_std_entries.h"
#include "demo_aml13x_core_entries.h"

/**
 * \brief �������
 */
void demo_hc32l13x_core_miniport_zlg72128_entry (void)
{
    AM_DBG_INFO("demo aml13x_core miniport zlg72128!\r\n");

    am_miniport_zlg72128_inst_init();

    demo_std_key_digitron_rngbuf_entry(0);
}
/** [src_miniport_zlg72128] */

/* end of file */