#ifndef __INC_FASTSPI_ARM_CC3200_h
#define __INC_FASTSPI_ARM_CC3200_h

#ifndef FASTLED_FORCE_SOFTWARE_SPI
#define FASTLED_ALL_PINS_HARDWARE_SPI
FASTLED_NAMESPACE_BEGIN

#include "driverlib/rom_map.h"
#include "driverlib/prcm.h"
#include "driverlib/spi.h"

#define ARM_HARDWARE_SPI

/*
CC3200 SPI Pins

GPIO#	SPI FUNC	QFN Pin#	Mod Pin#	PINMUX Mode
14		CLK			5			5			7
15		MISO		6			6			7
16		MOSI		7			7			7
17		CS			8			8			7

31		CLK			45			--------	7
30		MISO		53			42			7
32		MOSI		52			--------	8
0		CS			50			44			9
*/

template <uint8_t _DATA_PIN, uint8_t _CLOCK_PIN, uint8_t _SPI_CLOCK_DIVIDER>
class ARMHardwareSPIOutput {
  Selectable *m_pSelect;

  static inline void enable_pins(void) __attribute__((always_inline)) {	
	//configure Data & Clock pins (using PINMUX) to output to SPI
	switch(_DATA_PIN) {
		case 7: MAP_PinTypeSPI(PIN_07, PIN_MODE_7); break;
		case 52: MAP_PinTypeSPI(PIN_52, PIN_MODE_8); break;
		default: UART_PRINT("Error: Wrong data pin tried to enable SPI. Use pin 7 or 52.");
    }

    switch(_CLOCK_PIN) {
		case 5: MAP_PinTypeSPI(PIN_05, PIN_MODE_7); break;
		case 45: MAP_PinTypeSPI(PIN_45, PIN_MODE_7); break;
		default: UART_PRINT("Error: Wrong clock pin tried to enable SPI. Use Pin 5 or 45.")
    }
  }

  static inline void disable_pins(void) __attribute((always_inline)) {
    //MAP_SPIDisable(GSPI_BASE);
	//doesn't actually disable, just turns them back to GPIO. not sure if this is most efficient way to disable
	switch(_DATA_PIN) {
      case 7: MAP_PinTypeGPIO(PIN_07, PIN_MODE_0, false); break;		
      case 52: MAP_PinTypeGPIO(PIN_52, PIN_MODE_0, false); break;
    }

    switch(_CLOCK_PIN) {
		case 5: MAP_PinTypeGPIO(PIN_05, PIN_MODE_0, false); break;
		case 45: MAP_PinTypeGPIO(PIN_45, PIN_MODE_0, false); break;
    }
  }

public:
  ARMHardwareSPIOutput() { m_pSelect = NULL; }
  ARMHardwareSPIOutput(Selectable *pSelect) { m_pSelect = pSelect; }

  // set the object representing the selectable
  void setSelect(Selectable *pSelect) { m_pSelect = pSelect; }

  // initialize the SPI subssytem
  void init() {
	//Enable clock to SPI module
	//TODO: Determine if just want to run SPI at max value of 40 (48?) MHz. Defaults to 24 MHz w/ APA102
	MAP_PRCMPeripheralClkEnable(PRCM_GSPI, PRCM_RUN_MODE_CLK);	//Set SPI to internal clock source
	
	//converts the _SPI_CLOCK_DIVIDER into a bit rate, which is then converted back to divider in SPIConfigSetExpClock func
	//A little roundabout, but needed to maintain compatibility, unfortunately. If changed macro in fastspi.h, would affect SoftwareSPI as well
	unsigned long spibitrate = ((MAP_PRCMPeripheralClockGet(PRCM_GSPI))/(_SPI_CLOCK_DIVIDER));	
	enable_pins();
	
	//Soft reset SPI module & initialize
	MAP_PRCMPeripheralReset(PRCM_GSPI);
	MAP_SPIReset(GSPI_BASE);
	MAP_SPIConfigSetExpClk(GSPI_BASE, MAP_PRCMPeripheralClockGet(PRCM_GSPI), \
		spibitrate, SPI_MODE_MASTER, SPI_SUB_MODE_0, \
		(SPI_SW_CTRL_CS | SPI_3PIN_MODE | SPI_TURBO_OFF | SPI_CS_ACTIVELOW | SPI_WL_8));
	//Configure SPI for 24 MHz, Master, Mode 0, and a bunch of flags
	//Force 8 bit words
	//TODO: decide if I want to use internal SPI FIFO
	
	MAP_SPIEnable(GSPI_BASE);
  }

  // latch the CS select
  void inline select() __attribute__((always_inline)) {
    if(m_pSelect != NULL) { m_pSelect->select(); }
    enable_pins();
  }


  // release the CS select
  void inline release() __attribute__((always_inline)) {
    disable_pins();
    if(m_pSelect != NULL) { m_pSelect->release(); }
  }

  // Wait for the word to be clear
  static void wait() __attribute__((always_inline)) { while(!(HWREG(ulBase + MCSPI_O_CH0STAT)&MCSPI_CH0STAT_TXS));  }

  // wait until all queued up data has been written
  void waitFully() { wait(); }

  // not the most efficient mechanism in the world - but should be enough for sm16716 and friends
  template <uint8_t BIT> inline static void writeBit(uint8_t b) { /* TODO */ }

  // write a byte out via SPI (returns immediately on writing register)
  static void writeByte(uint8_t b) __attribute__((always_inline)) { MAP_SPIDataPut(GSPI_BASE, b); }
  // write a word out via SPI (returns immediately on writing register)
  static void writeWord(uint16_t w) __attribute__((always_inline)) { writeByte(w>>8); writeByte(w & 0xFF); }

  // A raw set of writing byte values, assumes setup/init/waiting done elsewhere (static for use by adjustment classes)
  static void writeBytesValueRaw(uint8_t value, int len) {
    while(len--) { writeByte(value); }
  }

  // A full cycle of writing a value for len bytes, including select, release, and waiting
  void writeBytesValue(uint8_t value, int len) {
    select();
    while(len--) {
      writeByte(value);
    }
    waitFully();
    release();
  }

  // A full cycle of writing a raw block of data out, including select, release, and waiting
  template <class D> void writeBytes(register uint8_t *data, int len) {
    uint8_t *end = data + len;
    select();
    //TODO: could be optimized to use MAP_SPITransfer()
    while(data != end) {
      writeByte(D::adjust(*data++));
    }
    D::postBlock(len);
    waitFully();
    release();
  }

  void writeBytes(register uint8_t *data, int len) { writeBytes<DATA_NOP>(data, len); }


  template <uint8_t FLAGS, class D, EOrder RGB_ORDER> void writePixels(PixelController<RGB_ORDER> pixels) {
    int len = pixels.mLen;

    select();
    while(pixels.has(1)) {
      if(FLAGS & FLAG_START_BIT) {
        writeBit<0>(1);
        writeByte(D::adjust(pixels.loadAndScale0()));
        writeByte(D::adjust(pixels.loadAndScale1()));
        writeByte(D::adjust(pixels.loadAndScale2()));
      } else {
        writeByte(D::adjust(pixels.loadAndScale0()));
        writeByte(D::adjust(pixels.loadAndScale1()));
        writeByte(D::adjust(pixels.loadAndScale2()));
      }

      pixels.advanceData();
      pixels.stepDithering();
    }
    D::postBlock(len);
    release();
  }

};

FASTLED_NAMESPACE_END

#endif //FASTLED_FORCE_SOFTWARE_SPI

#endif //__INC_FASTSPI_ARM_CC3200_h
