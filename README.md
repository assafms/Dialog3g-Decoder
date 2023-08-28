custom component for ESPHOME + PCB for the Dialog3G decoder

the parameters for the XL4432 IC were a guess based on some RF reverse engineering using HackRF
and are specific the the Israeli version of the water meter. 

original post i made a few years ago with more details : https://www.reddit.com/r/RTLSDR/comments/10nri4r/dialog3g_water_meter_reading_now_with_dedicated/

Post: 

a few years ago I managed to remotely read my ARAD Dialog3G water meter using low cost SDR.
from the old post:
" in Israel they are using 916.3Mhz , FSK , Manchester encoding . i use this command in RTL_433 to extract the data : rtl_433 -f 916.0M -s 2400000 -g 280 -a 4 -X "n=myname,modulation=FSK_MC_ZEROBIT,s=8.4,l=8.4,r=3000,invert"  

this has been feeding the results to my home assistance using MQTT , but the need for a raspberry pi just to run the rtl_433 and the SDR was a bit annoying. so using a few images of the device approval from the FCC I recognized the logo of Silicon Labs. guessed that the device they used is SI4432 and purchased one for 2$ in AliExpress. to get the device to work you need to connect to it using SPI , a 4$ ESP32 "Arduino" will do fine and will also push the result back using WiFi. the connections are similar to this https://static.rcgroups.net/forums/attachments/4/0/8/5/8/3/a6555159-46-openlrs.jpg  

To config the SI4432 registers , first calculate the values using the great SIlabs excel they have "Si4432-Register-Settings_RevV-v26" (you can find it in google). the configuration that works in Israel is:

Modem registers tab  
Modulation: FSK  
Manchester: ON  
Carrier: 916.3  
RB: 59.45kbps  
AFC: Disable  
Freq deviation: 175Khz  
Rx BW: 600Khz  
PH+ FIFO Mode tab  
Enable CRC: No  
Data invertion:OFF  
Manchester invert:No  
Preamble Polarity: 01010  
Headers: No header  
Variable packet length: No  
Preamble length : 7  
Sync words : for me [0x3E,0x69] works , might be different for you  
Data length : 0x14  




The end result that works for me :

SpiWriteRegister(0x1C, 0x8C)  
SpiWriteRegister(0x1D, 0x00)  
SpiWriteRegister(0x20, 0x65)  
SpiWriteRegister(0x21, 0x00)  
SpiWriteRegister(0x22, 0xA2)  
SpiWriteRegister(0x23, 0x57)  
SpiWriteRegister(0x24, 0x00)  
SpiWriteRegister(0x25, 0xDE)  
SpiWriteRegister(0x30, 0xA8)  
SpiWriteRegister(0x32, 0x8C)  
SpiWriteRegister(0x33, 0x0A)  
SpiWriteRegister(0x34, 0x07)  
SpiWriteRegister(0x35, 0x3A)  
SpiWriteRegister(0x36, 0x3e)  
SpiWriteRegister(0x37, 0x69)  
SpiWriteRegister(0x3E, 0x28)  
SpiWriteRegister(0x6E, 0x01)  
SpiWriteRegister(0x6F, 0x3B)  
SpiWriteRegister(0x70, 0x22)  
SpiWriteRegister(0x71, 0x26)  
SpiWriteRegister(0x72, 0x18)  
SpiWriteRegister(0x75, 0x75)  
SpiWriteRegister(0x76, 0xCB)  
SpiWriteRegister(0x77, 0xC0)  



Once you set the registers , you still need to start the RX engine , watch for the interrupts and read the fifo when a new packet is in . I will release my awful Arduino code in a few weeks . more info on this process is in the "AN415_DS.pdf" also available in Silabs site.  

bottom line  

With ~6$ of hardware , you can remotely read the Israeli version of the Dialog3G water meter. No need (in this specific case for an SDR)

