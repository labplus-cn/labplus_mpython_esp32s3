/*
 * RFID.cpp - Library to use ARDUINO RFID MODULE KIT 13.56 MHZ WITH TAGS SPI W AND R BY COOQROBOT.
 * Based on code Dr.Leong   ( WWW.B2CQSHOP.COM )
 * Created by Miguel Balboa, Jan, 2012.
 * Released into the public domain.
 * modify by chaohuijiang 
 * MFRC522芯片功能简要描述
 *   1、MFRC522支持三种通信接口：USART I2C SPI,芯片可跟据两个外接引脚的电平组合自动识别与MCU的接口，
 *   2、对所MFRC的所有操作都是通过对其内部寄存器的读写操作实现的，MCU与芯片的数据通信格式参看各接口描述章节。
 *   3、在MFRC522上存在两种通信：与MCU的通信（操作寄存器），与卡的通信，与卡的通信实际上也是MCU操作芯片寄存器实现的。
 * 库函数使用
 *  1、初始化
 *      调用RFID_init()函数做以下相关初始化
 *         复位
 *         打开天线。
 *  2、读写卡操作
 *     1)、调用RFID_findCard()寻卡，并获得卡类型。
 *     2)、调用RFID_anticoll()进行防冲突检测，并获得卡序列号。
 *     3)、调用RFID_selectTag()选择并锁定卡，此后的各操作MFRC522会保持与锁定的卡进行交互。
 *     4）调用RFID_auth()进行卡密验证
 *     4)、调用以下操作函数对卡读写：
 *         RFID_readBlock()  读卡上给定地址的块数据到缓冲
 *         RFID_writeBlock()   写缓存数据到给定地址的块
 *         RFID_IncDecCardBlock()  对给定地址块增值、减值操作（钱包充值、扣钱）
 *         RFID_backupCardBlock()  数据块备份（钱包备份)
 *     5)、操作完成后，调用halt()使卡进入halt状态。
 *         
 */

/******************************************************************************
 * 包含文件
 ******************************************************************************/
#include "mfrc522.h"
#include "esp_board_periph.h"
#include "freertos/FreeRTOS.h"
/******************************************************************************
 * 用户 API
 ******************************************************************************/
static int writeReg(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t reg_addr, uint8_t val);
static uint8_t readReg(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t reg_addr);
/******************************************************************************
 * 函 数 名：init
 * 功能描述：初始化RC522
 * 输入参数：无
 * 返 回 值：无
 ******************************************************************************/
void RFID_init(i2c_master_dev_handle_t _rfid_dev_handle)
{    
  RFID_reset(_rfid_dev_handle);
  RFID_antennaOn(_rfid_dev_handle);    //打开天线
}

/******************************************************************************
 * 函 数 名：reset
 * 功能描述：复位RC522
 * 输入参数：无
 * 返 回 值：无
 ******************************************************************************/
void RFID_reset(i2c_master_dev_handle_t _rfid_dev_handle)
{
  // HAL_GPIO_WritePin(MF_RST_GPIO_Port, MF_RST_Pin, GPIO_PIN_RESET);
  // HAL_Delay(1);
  // HAL_GPIO_WritePin(MF_RST_GPIO_Port, MF_RST_Pin, GPIO_PIN_SET);
  // HAL_Delay(1);
  
  writeReg(_rfid_dev_handle, CommandReg, PCD_RESETPHASE);
  // HAL_Delay(1);
  
  /* imer: TPrescaler*TreloadVal/6.78MHz = 24ms */
  writeReg(_rfid_dev_handle, TModeReg, 0x8D);   //Tauto=1; f(Timer) = 6.78MHz/TPreScaler
  writeReg(_rfid_dev_handle, TPrescalerReg, 0x3E);  //TModeReg[3..0] + TPrescalerReg
  writeReg(_rfid_dev_handle, TReloadRegL, 30);
  writeReg(_rfid_dev_handle, TReloadRegH, 0);
  writeReg(_rfid_dev_handle, TxAutoReg, 0x40);    //100%ASK 必须要
  writeReg(_rfid_dev_handle, ModeReg, 0x3D);    // CRC valor inicial de 0x6363

  //ClearBitMask(Status2Reg, 0x08); //MFCrypto1On=0
  //writeReg(RxSelReg, 0x86);   //RxWait = RxSelReg[5..0]
  //writeReg(RFCfgReg, 0x7F);     //RxGain = 48dB
}

/******************************************************************************
* 函 数 名：antennaOn
* 功能描述：开启天线,每次启动或关闭天险发射之间应至少有1ms的间隔
* 输入参数：无
* 返 回 值：无
******************************************************************************/
void RFID_antennaOn(i2c_master_dev_handle_t _rfid_dev_handle)
{
  uint8_t temp;

  temp = readReg(_rfid_dev_handle, TxControlReg);
  if (!(temp & 0x03))
  {
    RFID_setBitMask(_rfid_dev_handle, TxControlReg, 0x03);
  }
//	temp = readReg(TxControlReg);
//	temp = readReg(TxControlReg);
}

/******************************************************************************
* 函 数 名：antennaOff
* 功能描述：关闭天线,每次启动或关闭天险发射之间应至少有1ms的间隔
* 输入参数：无
* 返 回 值：无
******************************************************************************/
void RFID_antennaOff(i2c_master_dev_handle_t _rfid_dev_handle)
{
  uint8_t temp;

  temp = readReg(_rfid_dev_handle, TxControlReg);
  if (!(temp & 0x03))
  {
      RFID_clearBitMask(_rfid_dev_handle, TxControlReg, 0x03);
  }
}

/******************************************************************************
* 函 数 名：selectTag
* 功能描述：选卡，读取卡存储器容量
* 输入参数：serNum--传入卡序列号
* 返 回 值：成功返回卡容量
******************************************************************************/
uint8_t RFID_selectTag(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t *serNum)
{
  uint8_t i;
  uint8_t status;
  uint8_t size;
  unsigned int recvBits;
  uint8_t buffer[9];

  //ClearBitMask(Status2Reg, 0x08);                        //MFCrypto1On=0

  buffer[0] = PICC_SElECTTAG;
  buffer[1] = 0x70;

  for (i = 0; i<5; i++)
    buffer[i + 2] = *(serNum + i);

  RFID_calculateCRC(_rfid_dev_handle, buffer, 7, &buffer[7]);

  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);
  if ((status == MI_OK) && (recvBits == 0x18))
    size = buffer[i];
  else
    size = 0;
  return size;
}

/******************************************************************************
* 函 数 名：readBlock
* 功能描述：读卡中块数据
* 输入参数：blockAddr--块地址;recvData--读出的块数据
* 返 回 值：成功返回MI_OK
******************************************************************************/
uint8_t RFID_readBlock(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t blockAddr, uint8_t *recvData)
{
  uint8_t status;
  unsigned int unLen;
  uint8_t tempBuff[16];

  tempBuff[0] = PICC_READ;
  tempBuff[1] = blockAddr;
  RFID_calculateCRC(_rfid_dev_handle, tempBuff, 2, &tempBuff[2]);
  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, tempBuff, 4, recvData, &unLen);

  if ((status != MI_OK) || (unLen != 0x90)) {
    status = MI_ERR;
  }

  return status;
}

/******************************************************************************
* 函 数 名：writeBlock
* 功能描述：向卡写块数据
* 输入参数：blockAddr--块地址;writeData--向块写16字节数据
* 返 回 值：成功返回MI_OK
******************************************************************************/
uint8_t RFID_writeBlock(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t blockAddr, uint8_t *writeData)
{
  uint8_t status;
  unsigned int recvBits;
  uint8_t i;
  uint8_t buff[18];

  buff[0] = PICC_WRITE;
  buff[1] = blockAddr;
  RFID_calculateCRC(_rfid_dev_handle, buff, 2, &buff[2]);
  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 4, buff, &recvBits);

  if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))
    status = MI_ERR;

  if (status == MI_OK)
  {
    for (i = 0; i<16; i++)    //?FIFO?16Byte?? Datos a la FIFO 16Byte escribir
      buff[i] = *(writeData + i);

    RFID_calculateCRC(_rfid_dev_handle, buff, 16, &buff[16]);
    status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 18, buff, &recvBits);

    if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))
      status = MI_ERR;
  }

  return status;
}

/******************************************************************************
* 函 数 名：IncDecCardBlock
* 功能描述：卡块增减值（电子钱包充值、扣款）
* 输入参数：blockAddr--块地址
*          dd_mode：增值or减值
*          *pValue： 增减值大小
* 返 回 值：
******************************************************************************/
uint8_t RFID_IncDecCardBlock(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t dd_mode, uint8_t blockAddr, int32_t pValue)
{
  char status;
  unsigned int  unLen;
  uint8_t buff[16];

  buff[0] = dd_mode;
  buff[1] = blockAddr;
  RFID_calculateCRC(_rfid_dev_handle, buff, 2, &buff[2]);

  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 4, buff, &unLen);

  if ((status != MI_OK) || (unLen != 4) || ((buff[0] & 0x0F) != 0x0A))
  {
    status = MI_ERR;
  }

  if (status == MI_OK)
  {
    // memcpy(ucComMF522Buf, pValue, 4);
    // for (i = 0; i<4; i++)
    // {
    //   buff[i] = *(pValue + i);
    // }
    buff[0] = (uint8_t)(pValue & 0xff);
    buff[1] = (uint8_t)((pValue >> 8) & 0xff);
    buff[2] = (uint8_t)((pValue >> 16) & 0xff);
    buff[3] = (uint8_t)((pValue >> 24) & 0xff);
    RFID_calculateCRC(_rfid_dev_handle, buff, 4, &buff[4]);
    unLen = 0;
    status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 6, buff, &unLen);
    if (status != MI_ERR)
    {
      status = MI_OK;
    }
  }

  if (status == MI_OK)
  {
    buff[0] = PICC_TRANSFER;
    buff[1] = blockAddr;
    RFID_calculateCRC(_rfid_dev_handle, buff, 2, &buff[2]);

    status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 4, buff, &unLen);

    if ((status != MI_OK) || (unLen != 4) || ((buff[0] & 0x0F) != 0x0A))
    {
      status = MI_ERR;
    }
  }
  return status;
}

/******************************************************************************
* 函 数 名：backupCardBlock
* 功能描述： 卡块备份
* 输入参数：sourceaddr--源地址
*          goaladdr--目标地址
* 返 回 值：
******************************************************************************/
uint8_t RFID_backupCardBlock(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t sourceaddr, uint8_t goaladdr)
{
  char status;
  unsigned int  unLen;
  uint8_t buff[16];

  buff[0] = PICC_RESTORE;
  buff[1] = sourceaddr;
  RFID_calculateCRC(_rfid_dev_handle, buff, 2, &buff[2]);

  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 4, buff, &unLen);

  if ((status != MI_OK) || (unLen != 4) || ((buff[0] & 0x0F) != 0x0A))
  {
    status = MI_ERR;
  }

  if (status == MI_OK)
  {
    buff[0] = 0;
    buff[1] = 0;
    buff[2] = 0;
    buff[3] = 0;
    RFID_calculateCRC(_rfid_dev_handle, buff, 4, &buff[4]);

    status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 6, buff, &unLen);
    if (status != MI_ERR)
    {
      status = MI_OK;
    }
  }

  if (status != MI_OK)
  {
    return MI_ERR;
  }

  buff[0] = PICC_TRANSFER;
  buff[1] = goaladdr;

  RFID_calculateCRC(_rfid_dev_handle, buff, 2, &buff[2]);

  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 4, buff, &unLen);

  if ((status != MI_OK) || (unLen != 4) || ((buff[0] & 0x0F) != 0x0A))
  {
    status = MI_ERR;
  }

  return status;
}

/******************************************************************************
* 函 数 名：Halt
* 功能描述：命令卡片进入休眠状态
* 输入参数：无
* 返 回 值：无
******************************************************************************/
uint8_t RFID_halt(i2c_master_dev_handle_t _rfid_dev_handle)
{
  uint8_t status;
  unsigned int unLen;
  uint8_t buff[4];

  buff[0] = PICC_HALT;
  buff[1] = 0;
  RFID_calculateCRC(_rfid_dev_handle, buff, 2, &buff[2]);

  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, buff, 4, buff, &unLen);
  return status;
}

/******************************************************************************
 * 函 数 名：setBitMask
 * 功能描述：置RC522寄存器位
 * 输入参数：reg--寄存器地址;mask--置位值
 * 返 回 值：无
 ******************************************************************************/
void RFID_setBitMask(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t reg, uint8_t mask)
{
  uint8_t tmp;
  tmp = readReg(_rfid_dev_handle, reg);
  writeReg(_rfid_dev_handle, reg, tmp | mask);  // set bit mask
}

/******************************************************************************
 * 函 数 名：clearBitMask
 * 功能描述：清RC522寄存器位
 * 输入参数：reg--寄存器地址;mask--清位值
 * 返 回 值：无
 ******************************************************************************/
void RFID_clearBitMask(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t reg, uint8_t mask)
{
  uint8_t tmp;
  tmp = readReg(_rfid_dev_handle, reg);
  writeReg(_rfid_dev_handle, reg, tmp & (~mask));  // clear bit mask
}

/******************************************************************************
* 函 数 名：findCard
* 功能描述：寻卡，读取卡类型号
* 输入参数：reqMode--寻卡方式，
*           TagType--返回卡片类型
*                    0x4400 = Mifare_UltraLight
*                    0x0400 = Mifare_One(S50)
*                    0x0200 = Mifare_One(S70)
*                    0x0800 = Mifare_Pro(X)
*                    0x4403 = Mifare_DESFire
* 返 回 值：成功返回MI_OK
******************************************************************************/
uint8_t RFID_findCard(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t reqMode, uint8_t *TagType)
{
  uint8_t status;
  unsigned int backBits;      //接收到的数据位数

  writeReg(_rfid_dev_handle, BitFramingReg, 0x07);    //TxLastBists = BitFramingReg[2..0] ???

  TagType[0] = reqMode;
  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);

  if ((status != MI_OK) || (backBits != 0x10))
  {
    status = MI_ERR;
  }

  return status;
}

/******************************************************************************
* 函 数 名：anticoll
* 功能描述：防冲突检测，读取选中卡片的卡序列号
* 输入参数：serNum--返回4字节卡序列号,第5字节为校验字节
* 返 回 值：成功返回MI_OK
******************************************************************************/
uint8_t RFID_anticoll(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t *serNum)
{
  uint8_t status;
  uint8_t i;
  uint8_t serNumCheck = 0;
  unsigned int unLen;

  RFID_clearBitMask(_rfid_dev_handle, Status2Reg, 0x08);   //TempSensclear
  RFID_clearBitMask(_rfid_dev_handle, CollReg, 0x80);     //ValuesAfterColl
  writeReg(_rfid_dev_handle, BitFramingReg, 0x00);    //TxLastBists = BitFramingReg[2..0]

  serNum[0] = PICC_ANTICOLL;
  serNum[1] = 0x20;

  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);

  if (status == MI_OK)
  {
    //校验卡序列号
    for (i = 0; i<4; i++){
      *(serNum + i) = serNum[i];
      serNumCheck ^= serNum[i];
    }
    if (serNumCheck != serNum[i]){
      status = MI_ERR;
    }
  }

  RFID_setBitMask(_rfid_dev_handle, CollReg, 0x80);    //ValuesAfterColl=1

  return status;
}

/******************************************************************************
 * 函 数 名：RFID_calculateCRC
 * 功能描述：用MF522计算CRC
 * 输入参数：pIndata--要读数CRC的数据，len--数据长度，pOutData--计算的CRC结果
 * 返 回 值：无
 ******************************************************************************/
void RFID_calculateCRC(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t *pIndata, uint8_t len, uint8_t *pOutData)
{
  uint8_t i, n;

  RFID_clearBitMask(_rfid_dev_handle, DivIrqReg, 0x04);      //CRCIrq = 0
  RFID_setBitMask(_rfid_dev_handle, FIFOLevelReg, 0x80);     //清FIFO指针
  //Write_MFRC522(CommandReg, PCD_IDLE);

  //向FIFO中写入数据
  for (i=0; i<len; i++)
    writeReg(_rfid_dev_handle, FIFODataReg, *(pIndata+i));
  writeReg(_rfid_dev_handle, CommandReg, PCD_CALCCRC);

  //等待CRC计算完成
  i = 0xFF;
  do
  {
    n = readReg(_rfid_dev_handle, DivIrqReg);
    i--;
  }
  while ((i!=0) && !(n&0x04));      //CRCIrq = 1

  //读取CRC计算结果
  pOutData[0] = readReg(_rfid_dev_handle, CRCResultRegL);
  pOutData[1] = readReg(_rfid_dev_handle, CRCResultRegM);
}

/******************************************************************************
 * 函 数 名：RFID_MFRC522ToCard
 * 功能描述：RC522和ISO14443卡通讯
 * 输入参数：command--MF522命令字，
 *           sendData--通过RC522发送到卡片的数据,
 *                     sendLen--发送的数据长度
 *                     backData--接收到的卡片返回数据，
 *                     backLen--返回数据的位长度
 * 返 回 值：成功返回MI_OK
 ******************************************************************************/
uint8_t RFID_MFRC522ToCard(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, unsigned int *backLen)
{
  uint8_t status = MI_ERR;
  uint8_t irqEn = 0x00;
  uint8_t waitIRq = 0x00;
  uint8_t lastBits;
  uint8_t n;
  unsigned int i;

  switch (command)
  {
    case PCD_AUTHENT:   //认证卡密
    {
      irqEn = 0x12;
      waitIRq = 0x10;
      break;
    }
    case PCD_TRANSCEIVE:  //发送FIFO中数据
    {
      irqEn = 0x77;
      waitIRq = 0x30;
      break;
    }
    default:
      break;
  }

  writeReg(_rfid_dev_handle, CommIEnReg, irqEn|0x80); //允许中断请求
  RFID_clearBitMask(_rfid_dev_handle, CommIrqReg, 0x80);       //清除所有中断请求位
  RFID_setBitMask(_rfid_dev_handle, FIFOLevelReg, 0x80);       //FlushBuffer=1, FIFO初始化

  writeReg(_rfid_dev_handle, CommandReg, PCD_IDLE);   //无动作，取消当前命令

  //向FIFO中写入数据
  for (i=0; i<sendLen; i++)
    writeReg(_rfid_dev_handle, FIFODataReg, sendData[i]);

  //执行命令
  writeReg(_rfid_dev_handle, CommandReg, command);
  if (command == PCD_TRANSCEIVE)
    RFID_setBitMask(_rfid_dev_handle, BitFramingReg, 0x80);    //StartSend=1,transmission of data starts

  //等待接收数据完成
  i = 2000; //i根据时钟频率调整，操作M1卡最大等待时间25ms
  do
  {
    //CommIrqReg[7..0]
    //Set1 TxIRq RxIRq IdleIRq HiAlerIRq LoAlertIRq ErrIRq TimerIRq
    n = readReg(_rfid_dev_handle, CommIrqReg);
    i--;
  }
  while ((i!=0) && !(n&0x01) && !(n&waitIRq));

  RFID_clearBitMask(_rfid_dev_handle, BitFramingReg, 0x80);      //StartSend=0

  if (i != 0)
  {
    if(!(readReg(_rfid_dev_handle, ErrorReg) & 0x1B)) //BufferOvfl Collerr CRCErr ProtecolErr
    {
      status = MI_OK;
      if (n & irqEn & 0x01)
        status = MI_NOTAGERR;     //??

      if (command == PCD_TRANSCEIVE)
      {
        n = readReg(_rfid_dev_handle, FIFOLevelReg);
        lastBits = readReg(_rfid_dev_handle, ControlReg) & 0x07;
        if (lastBits)
          *backLen = (n-1)*8 + lastBits;
        else
          *backLen = n*8;

        if (n == 0)
          n = 1;
        if (n > 16)
          n = 16;

        //读取FIFO中接收到的数据
        for (i=0; i<n; i++)
          backData[i] = readReg(_rfid_dev_handle, FIFODataReg);
      }
    }
    else
      status = MI_ERR;
  }

  //SetBitMask(ControlReg,0x80);           //timer stops
  //Write_MFRC522(CommandReg, PCD_IDLE);

  return status;
}

/******************************************************************************
 * 函 数 名：auth
 * 功能描述：验证卡片密码
 * 输入参数：authMode--密码验证模式
 *                     0x60 = 验证A密钥
 *                     0x61 = 验证B密钥
 *           BlockAddr--块地址
 *           Sectorkey--扇区密码
 *           serNum--卡片序列号，4字节
 * 返 回 值：成功返回MI_OK
 ******************************************************************************/
uint8_t RFID_auth(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum)
{
  uint8_t status;
  unsigned int recvBits;
  uint8_t i;
  uint8_t buff[12];

  //验证指令+块地址＋扇区密码＋卡序列号
  buff[0] = authMode;
  buff[1] = BlockAddr;
  for (i = 0; i < 6; i++)
	  /*buff[i + 2] = *(Sectorkey + i);*/
	  buff[i + 2] = 0xff;
  for (i = 0; i<4; i++)
	  buff[i + 8] = *(serNum + i);

  status = RFID_MFRC522ToCard(_rfid_dev_handle, PCD_AUTHENT, buff, 12, buff, &recvBits);
  if ((status != MI_OK) || (!(readReg(_rfid_dev_handle, Status2Reg) & 0x08)))
  {
	  status = MI_ERR;
  }

  return status;
}

static int writeReg(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t reg_addr, uint8_t val)
{
  uint8_t wbuf[2];
  wbuf[0] = reg_addr;
  wbuf[1] = val;
 return i2c_master_transmit(_rfid_dev_handle, wbuf, 2, 1000 / portTICK_PERIOD_MS);
}

static uint8_t readReg(i2c_master_dev_handle_t _rfid_dev_handle, uint8_t reg_addr)
{
	uint8_t buff; 

  i2c_master_transmit_receive(_rfid_dev_handle, &reg_addr, 1, &buff, 1, 1000 / portTICK_PERIOD_MS);

  return(buff);
}
