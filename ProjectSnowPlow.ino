
#include "Arduino.h"

#if 0
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

void setup()
{
    uint32_t brown_reg_temp = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG); //save WatchDog register
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
//    WiFi.mode(WIFI_MODE_STA); // turn on WiFi
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brown_reg_temp); //enable brownout detector
}

void loop()
{
    delay(-1);
}

#else

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "soc/soc.h"
//#include "soc/cpu.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_cntl.h"

#ifdef CONFIG_BROWNOUT_DET_LVL
#define BROWNOUT_DET_LVL CONFIG_BROWNOUT_DET_LVL
#else
#define BROWNOUT_DET_LVL 5
#endif //CONFIG_BROWNOUT_DET_LVL

#define CONFIG_BROWNOUT_DET_LVL_SEL_5 1


//--------------------------------------------------------------
//https://stackoverflow.com/questions/73200166/exception-handling-for-brownout-detector-was-trigerred

#include <rom/rtc.h>

void print_reset_reason(RESET_REASON reason)
{
  switch (reason)
  {
    /**<1, Vbat power on reset*/
    case 1 : Serial.println ("POWERON_RESET");break;          
    
    /**<3, Software reset digital core*/
    case 3 : Serial.println ("SW_RESET");break;               
    
    /**<4, Legacy watch dog reset digital core*/
    case 4 : Serial.println ("OWDT_RESET");break;             
    
    /**<5, Deep Sleep reset digital core*/
    case 5 : Serial.println ("DEEPSLEEP_RESET");break;        
    
    /**<6, Reset by SLC module, reset digital core*/
    case 6 : Serial.println ("SDIO_RESET");break;             
    
    /**<7, Timer Group0 Watch dog reset digital core*/
    case 7 : Serial.println ("TG0WDT_SYS_RESET");break;       
    
    /**<8, Timer Group1 Watch dog reset digital core*/
    case 8 : Serial.println ("TG1WDT_SYS_RESET");break;       
    
    /**<9, RTC Watch dog Reset digital core*/
    case 9 : Serial.println ("RTCWDT_SYS_RESET");break;       
    
    /**<10, Instrusion tested to reset CPU*/
    case 10 : Serial.println ("INTRUSION_RESET");break;       
    
    /**<11, Time Group reset CPU*/
    case 11 : Serial.println ("TGWDT_CPU_RESET");break;       
    
    /**<12, Software reset CPU*/
    case 12 : Serial.println ("SW_CPU_RESET");break;          
    
    /**<13, RTC Watch dog Reset CPU*/
    case 13 : Serial.println ("RTCWDT_CPU_RESET");break;      
    
    /**<14, for APP CPU, reseted by PRO CPU*/
    case 14 : Serial.println ("EXT_CPU_RESET");break;         
    
    /**<15, Reset when the vdd voltage is not stable*/
    case 15 : Serial.println ("RTCWDT_BROWN_OUT_RESET");break;
    
    /**<16, RTC Watch dog reset digital core and rtc module*/
    case 16 : Serial.println ("RTCWDT_RTC_RESET");break;      
    
    default : Serial.println ("NO_MEAN");
  }
}


//interrupt_handler_t void low_voltage_save(void *notused) {
 void low_voltage_save(void *notused) {
    int32_t saved_counter = 0;
    nvs_handle my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ets_printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        while(saved_counter < 100) {
            saved_counter++;
            err = nvs_set_i32(my_handle, "saved_counter", saved_counter);
            err = nvs_commit(my_handle);

        }
        // Close
        nvs_close(my_handle);
        REG_WRITE(RTC_CNTL_INT_CLR_REG, RTC_CNTL_BROWN_OUT_INT_CLR);
        esp_cpu_stall(!xPortGetCoreID());
        ets_printf("\r\nBrownout detector was triggered\r\n\r\n");
        //esp_restart_noos();
        while(1) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }


}



/*esp_err_t rtc_isr_register(intr_handler_t handler,
						  	 void* handler_arg,
                             uint32_t rtc_intr_mask, // set for brown out 
                             uint32_t flags);		 // =0, don't care about caching
*/

void brownout_init()
{
    REG_WRITE(RTC_CNTL_BROWN_OUT_REG,
            RTC_CNTL_BROWN_OUT_ENA /* Enable BOD */
            | RTC_CNTL_BROWN_OUT_PD_RF_ENA /* Automatically power down RF */
            /* Reset timeout must be set to >1 even if BOR feature is not used */
            | (2 << RTC_CNTL_BROWN_OUT_RST_WAIT_S)
            | (BROWNOUT_DET_LVL << RTC_CNTL_DBROWN_OUT_THRES_S));

    ESP_ERROR_CHECK( rtc_isr_register(low_voltage_save, 
                     				  NULL, 						//handler_arg
                                      RTC_CNTL_BROWN_OUT_INT_ENA_M, //rtc_intr_mask
                     				  0	)							//uint32_t flags
                    );

    printf("Initialized BOD\n");

    REG_SET_BIT(RTC_CNTL_INT_ENA_REG, RTC_CNTL_BROWN_OUT_INT_ENA_M);
}


//void app_main()
void setup()
{
    Serial.begin(115200);

    Serial.println("CPU0 reset reason: ");
    print_reset_reason(rtc_get_reset_reason(0));

    Serial.println("CPU1 reset reason: ");
    print_reset_reason(rtc_get_reset_reason(1));

    // Initialize NVS
    brownout_init();

    int32_t saved_counter = 0; // value will default to 0, if not set yet in NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    // Open
    printf("\n");
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle my_handle;
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("Done\n");

        // Read
        printf("Reading restart counter from NVS ... ");
        err = nvs_get_i32(my_handle, "saved_counter", &saved_counter);
        switch (err) {
            case ESP_OK:
                printf("Done\n");
                printf("Restart counter = %d\n", saved_counter);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                printf("The value is not initialized yet!\n");
                break;
            default :
                printf("Error (%s) reading!\n", esp_err_to_name(err));
        }

       // Close
        nvs_close(my_handle);
    }

    printf("\n");

    while (1) {
        printf("Restart counter = %d\n", saved_counter);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void loop()
{
    delay(-1);
}
#endif
