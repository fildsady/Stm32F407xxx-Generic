#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

#define USBD_VID                      0x0483
#define USBD_PID                      0x5730
#define USBD_LANGID_STRING            0x0409
#define USBD_MANUFACTURER_STRING      (uint8_t *)"STMicroelectronics"
#define USBD_PRODUCT_FS_STRING        (uint8_t *)"STM32 USB Audio"
#define USBD_CONFIGURATION_FS_STRING  (uint8_t *)"Audio Config"
#define USBD_INTERFACE_FS_STRING      (uint8_t *)"Audio Interface"
#define USB_LEN_STR_DESC_MAX          26U

uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef FS_Desc = {
  USBD_FS_DeviceDescriptor,
  USBD_FS_LangIDStrDescriptor,
  USBD_FS_ManufacturerStrDescriptor,
  USBD_FS_ProductStrDescriptor,
  USBD_FS_SerialStrDescriptor,
  USBD_FS_ConfigStrDescriptor,
  USBD_FS_InterfaceStrDescriptor,
};

/* USB Standard Device Descriptor */
__ALIGN_BEGIN uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
  0x12,                       /* bLength */
  USB_DESC_TYPE_DEVICE,       /* bDescriptorType */
  0x00, 0x02,                 /* bcdUSB = 2.00 */
  0x00,                       /* bDeviceClass (specified in interface descriptors) */
  0x00,                       /* bDeviceSubClass */
  0x00,                       /* bDeviceProtocol */
  USB_MAX_EP0_SIZE,           /* bMaxPacketSize */
  LOBYTE(USBD_VID), HIBYTE(USBD_VID), /* idVendor */
  LOBYTE(USBD_PID), HIBYTE(USBD_PID), /* idProduct */
  0x00, 0x01,                 /* bcdDevice = 1.00 */
  USBD_IDX_MFC_STR,           /* iManufacturer */
  USBD_IDX_PRODUCT_STR,       /* iProduct */
  USBD_IDX_SERIAL_STR,        /* iSerialNumber */
  USBD_MAX_NUM_CONFIGURATION  /* bNumConfigurations */
};

/* USB Language ID String Descriptor */
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
  USB_LEN_LANGID_STR_DESC,
  USB_DESC_TYPE_STRING,
  LOBYTE(USBD_LANGID_STRING),
  HIBYTE(USBD_LANGID_STRING),
};

__ALIGN_BEGIN uint8_t USBD_StringSerial[USB_LEN_STR_DESC_MAX] __ALIGN_END = {
  0x12,
  USB_DESC_TYPE_STRING,
  '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '1', 0
};

__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  UNUSED(speed);
  *length = sizeof(USBD_DeviceDesc);
  return USBD_DeviceDesc;
}

uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  UNUSED(speed);
  *length = sizeof(USBD_LangIDDesc);
  return USBD_LangIDDesc;
}

uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  UNUSED(speed);
  USBD_GetString(USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  UNUSED(speed);
  USBD_GetString(USBD_PRODUCT_FS_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  UNUSED(speed);
  *length = USBD_StringSerial[0];
  return USBD_StringSerial;
}

uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  UNUSED(speed);
  USBD_GetString(USBD_CONFIGURATION_FS_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  UNUSED(speed);
  USBD_GetString(USBD_INTERFACE_FS_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}
