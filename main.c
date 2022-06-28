/******************************************************************************
 * File Name:   main.c
 *
 * Description: This is the source code for PMG1 USB FS Billboard Code Example
 *              for ModusToolbox.
 *
 * Related Document: See README.md
 *
 *
*******************************************************************************
* Copyright 2021-2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/


/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_usb_dev.h"
#include "cycfg_usbdev.h"

/*******************************************************************************
* Macros
********************************************************************************/

/* LED GPIO selection based on PMG1 kit. For PMG1-S2, CY_DEVICE_CCG3 will be enabled */
#if (defined(CY_DEVICE_CCG3))
#define CYBSP_USER_LED_PORT         (GPIO_PRT1)
#define CYBSP_USER_LED_PIN          (3U)
#else
#define CYBSP_USER_LED_PORT         (GPIO_PRT5)
#define CYBSP_USER_LED_PIN          (5U)
#endif

/* LED toggle time */
#define LED_DELAY_MS                (500u)

/* Vddd threshold to enable internal regulators of USBFS block */ 
#define USB_REG_THRESHOLD           (3700U)

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void usb_high_isr(void);
static void usb_medium_isr(void);
static void usb_low_isr(void);

/*******************************************************************************
* Global Variables
********************************************************************************/
/* USB Interrupt Configuration */
const cy_stc_sysint_t usb_high_interrupt_cfg =
{
    .intrSrc = (IRQn_Type) usb_interrupt_hi_IRQn,
    .intrPriority = 0U,
};
const cy_stc_sysint_t usb_medium_interrupt_cfg =
{
    .intrSrc = (IRQn_Type) usb_interrupt_med_IRQn,
    .intrPriority = 1U,
};
const cy_stc_sysint_t usb_low_interrupt_cfg =
{
    .intrSrc = (IRQn_Type) usb_interrupt_lo_IRQn,
    .intrPriority = 2U,
};

/* USBDEV context variables */
cy_stc_usbfs_dev_drv_context_t  usb_drvContext;
cy_stc_usb_dev_context_t        usb_devContext;


/* PD Port Config */
cy_stc_usbpd_config_t PD_PORT0_config;

/* USBPD Context */
cy_stc_usbpd_context_t usbpd_context;

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  This is the main function. It initializes the USB Device block and enumerates
*  as a Billboard device. The LED blinks once the device enumerates successfully.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    uint32_t vddd = 0;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize the USBPD driver to read Vddd value. 
     * This call uses the SAR ADC present in the USBPD block
     * to measure the Vddd value. Based on the Vddd measured,
     * the internal regulators are enabled in the USBFS block. 
     */

#if defined(CY_DEVICE_CCG3)
    Cy_USBPD_Init(&usbpd_context, 0, mtb_usbpd_port0_HW, NULL,
            (cy_stc_usbpd_config_t *)&mtb_usbpd_port0_config, usbpd_context.dpmGetConfig);
#else
    Cy_USBPD_Init(&usbpd_context, 0, mtb_usbpd_port0_HW, mtb_usbpd_port0_HW_TRIM,
            (cy_stc_usbpd_config_t *)&mtb_usbpd_port0_config, usbpd_context.dpmGetConfig);

#endif /* CY_DEVICE_CCG3 */

    vddd = usbpd_context.adcVdddMv[CY_USBPD_ADC_ID_0];

    if(vddd > USB_REG_THRESHOLD)
    {
        Cy_USBFS_Dev_Drv_RegEnable(CYBSP_USB_HW, &usb_drvContext);
    }
    else
    {
        Cy_USBFS_Dev_Drv_RegDisable(CYBSP_USB_HW, &usb_drvContext);
    }

    /* Initialize the USB device */
    Cy_USB_Dev_Init(CYBSP_USB_HW, &CYBSP_USB_config, &usb_drvContext,
                    &usb_devices[0], &usb_devConfig, &usb_devContext);

    /* Initialize the USB interrupts */
    Cy_SysInt_Init(&usb_high_interrupt_cfg,   &usb_high_isr);
    Cy_SysInt_Init(&usb_medium_interrupt_cfg, &usb_medium_isr);
    Cy_SysInt_Init(&usb_low_interrupt_cfg,    &usb_low_isr);   

    /* Enable the USB interrupts */
    NVIC_EnableIRQ(usb_high_interrupt_cfg.intrSrc);
    NVIC_EnableIRQ(usb_medium_interrupt_cfg.intrSrc);
    NVIC_EnableIRQ(usb_low_interrupt_cfg.intrSrc);

    Cy_GPIO_SetHSIOM(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, HSIOM_SEL_GPIO);
    Cy_GPIO_SetDrivemode(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, CY_GPIO_DM_STRONG);

    /* Make device appear on the bus. This function call is blocking, 
     * it waits untill the device enumerates 
     */
    Cy_USB_Dev_Connect(true, CY_USB_DEV_WAIT_FOREVER, &usb_devContext);

    for(;;)
    {
        /* Toggle the user LED state */
        Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);

        /* Wait for 0.5 seconds */
        Cy_SysLib_Delay(LED_DELAY_MS);
    }
}

/***************************************************************************
* Function Name: usb_high_isr
********************************************************************************
* Summary:
*  This function process the high priority USB interrupts.
*
***************************************************************************/
static void usb_high_isr(void)
{
    /* Call interrupt processing */
    Cy_USBFS_Dev_Drv_Interrupt(CYBSP_USB_HW, 
                               Cy_USBFS_Dev_Drv_GetInterruptCauseHi(CYBSP_USB_HW), 
                               &usb_drvContext);
}


/***************************************************************************
* Function Name: usb_medium_isr
********************************************************************************
* Summary:
*  This function process the medium priority USB interrupts.
*
***************************************************************************/
static void usb_medium_isr(void)
{
    /* Call interrupt processing */
    Cy_USBFS_Dev_Drv_Interrupt(CYBSP_USB_HW, 
                               Cy_USBFS_Dev_Drv_GetInterruptCauseMed(CYBSP_USB_HW), 
                               &usb_drvContext);
}


/***************************************************************************
* Function Name: usb_low_isr
********************************************************************************
* Summary:
*  This function process the low priority USB interrupts.
*
**************************************************************************/
static void usb_low_isr(void)
{
    /* Call interrupt processing */
    Cy_USBFS_Dev_Drv_Interrupt(CYBSP_USB_HW, 
                               Cy_USBFS_Dev_Drv_GetInterruptCauseLo(CYBSP_USB_HW), 
                               &usb_drvContext);
}


/* [] END OF FILE */
