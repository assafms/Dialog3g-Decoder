ESPHOME - Dialog 3G decoder 

This is a custom component for ESPHOME for the Dialog3G decoder board i created. 
the parameters for the XL4432 IC were a guess based on some RF reverse engineering using HackRF
and are specific the the Israeli version of the water meter. 

Installation
--------------
copy the directory to the custom_components directory under ESPHOME (or create it)
Final directory structure in Home assistant 

config

	esphome
 
		watermeter.yaml
  
		secrets.yaml
  
		custom_components
  
			xl4432_spi_sensor
   
				__init__.py
    
				sensor.py
    
				xl4432.cpp
    
				xl4432.h
    
				xl4432_spi_sensor.cpp
    
				xl4432_spi_sensor.h
    


Important !
************************************************
* Change the METER_ID in xl4432_spi_sensor.cpp *
************************************************
