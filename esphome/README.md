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
    





Configuration
--------------
The meter ID can now be configured in the YAML file instead of editing the C++ code.

In your watermeter.yaml file, add the meter_id parameter to the xl4432_spi_sensor configuration:

```yaml
sensor:
  - platform: xl4432_spi_sensor
    name: xl4432
    cs_pin: GPIO15
    meter_id: "0x4E61BC"
```

The meter_id format is a single hex string:
- "0x4E61BC" (with 0x prefix)
- "4E61BC" (without 0x prefix)

To find your meter ID:
1. Look for the meter ID on your water meter (usually in decimal format)
2. Convert it to hexadecimal (e.g., 12345678 → 0xBC614E)
3. Reverse the byte order: 0xBC614E → 0x4E61BC
4. Use the reversed hex value in the YAML

Example conversion:
- Meter ID = 12345678 (decimal)
- Convert to hex: 0xBC614E
- Reverse byte order: 0x4E61BC
- Use in YAML: meter_id: "0x4E61BC"
