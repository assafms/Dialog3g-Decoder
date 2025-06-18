ESPHOME - Dialog 3G decoder 

This is a custom component for ESPHOME for the Dialog3G decoder board i created. 
the parameters for the XL4432 IC were a guess based on some RF reverse engineering using HackRF
and are specific the the Israeli version of the water meter. 



Update 18.6.25
--------------
Added the ability to sniff all meter IDs , change the ID to "123456" to enable
Added logging to syslog of the raw packets while sniffing . recommended use of visual syslog server (windows) to view 





Installation
--------------
copy the directory to the custom_components directory under ESPHOME (or create it)
Final directory structure in Home assistant 

|-config  
|---esphome  
|-----watermeter.yaml  
|-----secrets.yaml  
|-----custom_components 
|-------syslog
|---------__init__.py  
|---------syslog_component.cpp  
|---------syslog_component.h  
|-------xl4432_spi_sensor  
|---------__init__.py  
|---------sensor.py  
|---------xl4432.cpp  
|---------xl4432.h  
|---------xl4432_spi_sensor.cpp  
|---------xl4432_spi_sensor.h  
    





Important !
************************************************
* Change the METER_ID in xl4432_spi_sensor.cpp *
************************************************
