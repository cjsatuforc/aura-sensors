/* Binary I/O section: generial info ...
 * Packets start with two bytes ... START_OF_MSG0 and START_OF_MSG1
 * Following that is the packet ID
 * Following that is the packet data size (not including start bytes or check sum, just the data)
 * Following that is the actual data packet
 * Following that is a two byte check sum.  The check sum includes the packet id and size as well as the data.
 */

#include "setup_msg.h"
#include "structs.h"


void ugear_cksum( byte hdr1, byte hdr2, const byte *buf, byte size,
                  byte *cksum0, byte *cksum1 )
{
    byte c0 = 0;
    byte c1 = 0;

    c0 += hdr1;
    c1 += c0;

    c0 += hdr2;
    c1 += c0;

    for ( byte i = 0; i < size; i++ ) {
        c0 += (byte)buf[i];
        c1 += c0;
    }

    *cksum0 = c0;
    *cksum1 = c1;
}


bool parse_message_bin( byte id, byte *buf, byte message_size )
{
    bool result = false;

    // Serial.print("message id = "); Serial.print(id); Serial.print(" len = "); Serial.println(message_size);
    
    if ( id == FLIGHT_COMMAND_PACKET_ID && message_size == AP_CHANNELS * 2 ) {
        /* flight commands are 2 byte ints, normalized, then scaled to +/- 16384 */
        float ap_tmp[AP_CHANNELS];
        for ( int i = 0; i < AP_CHANNELS; i++ ) {
            int16_t val = *(int16_t *)buf; buf += 2;
            ap_tmp[i] = (float)val / 16384.0;
            //Serial1.println(ap_tmp[i]);
        }
        // autopilot_norm uses the same channel mapping as sbus_norm,
        // so map ap_tmp values to their correct places in
        // autopilot_norm
        autopilot_norm[0] = receiver_norm[0]; // auto/manual swith
        autopilot_norm[1] = receiver_norm[1]; // throttle enable
        autopilot_norm[2] = ap_tmp[0];        // throttle
        autopilot_norm[3] = ap_tmp[1];
        autopilot_norm[4] = ap_tmp[2];
        autopilot_norm[5] = ap_tmp[3];
        autopilot_norm[6] = ap_tmp[4];
        autopilot_norm[7] = ap_tmp[5];

        if ( receiver_norm[0] > 0.0 ) {
            // autopilot mode active (determined elsewhere when each
            // new receiver frame is ready) mix the inputs and write
            // the actuator outputs now
            sas_update( autopilot_norm );
            mixing_update( autopilot_norm );
            pwm_update();
        } else {
            // manual mode, do nothing with actuator commands from the
            // autopilot
        }
        result = true;
    } else if ( id == CONFIG_PACKET_ID && message_size == sizeof(config) ) {
        Serial.println("received new config");
        config = *(config_t *)buf;
        pwm_setup();  // reset pwm rates in case they've been changed
        write_ack_bin( id, 0 );
        result = true;
    } else if ( id == WRITE_EEPROM_PACKET_ID && message_size == 0 ) {
        Serial.println("received update eeprom command");
        config_write_eeprom();
        write_ack_bin( id, 0 );
        result = true;
    } else {
        // Serial.print("unknown message id = "); Serial.print(id); Serial.print(" len = "); Serial.println(message_size);
    }
    return result;
}


bool read_commands() {
    #define MAX_CMD_BUFFER 256
    static byte cmd_buffer[MAX_CMD_BUFFER];
    
    // 0 = looking for SOM0
    // 1 = looking for SOM1
    // 2 = looking for packet id & size
    // 3 = looking for packet data
    // 4 = looking for checksum
    static byte state = 0;
    byte input;
    static int buf_counter = 0;
    static byte message_id = 0;
    static byte message_size = 0;
    byte cksum0 = 0, cksum1 = 0;
    bool new_data = false;
    // Serial.print("start read_commands(): "); Serial.println(state);

    if ( state == 0 ) {
        while ( Serial1.available() >= 1 ) {
            // scan for start of message
            input = Serial1.read();
            if ( input == START_OF_MSG0 ) {
                // Serial.println("start of msg0");
                state = 1;
                break;
            }
        }
    }
    if ( state == 1 ) {
        if ( Serial1.available() >= 1 ) {
            input = Serial1.read();
            if ( input == START_OF_MSG1 ) {
                // Serial.println("start of msg1");
                state = 2;
            } 
            else if ( input == START_OF_MSG0 ) {
                // no change
            } else {
                // oops
                state = 0;
            }
        }
    }
    if ( state == 2 ) {
        if ( Serial1.available() >= 2 ) {
            message_id = Serial1.read();
            // Serial.print("id="); Serial.println(message_id);
            message_size = Serial1.read();
            // Serial.print("size="); Serial.println(message_size);
            if ( message_size > 200 ) {
                // ignore nonsensical sizes
                state = 0;
            }  else {
                state = 3;
                buf_counter = 0;
            }
        }
    }
    if ( state == 3 ) {
        while ( Serial1.available() >= 1 && buf_counter < message_size ) {
            if ( buf_counter < MAX_CMD_BUFFER ) {
                cmd_buffer[buf_counter] = Serial1.read();
                buf_counter++;
                // Serial.println(buf[i], DEC);
            } else {
                state = 0;
            }
        }
        if ( buf_counter >= message_size ) {
            state = 4;
        }
    }
    if ( state == 4 ) {
        if ( Serial1.available() >= 2 ) {
            cksum0 = Serial1.read();
            cksum1 = Serial1.read();
            byte new_cksum0, new_cksum1;
            ugear_cksum( message_id, message_size, cmd_buffer, message_size, &new_cksum0, &new_cksum1 );
            if ( cksum0 == new_cksum0 && cksum1 == new_cksum1 ) {
                // Serial.println("passed check sum!");
                // Serial.print("size="); Serial.println(message_size);
                parse_message_bin( message_id, cmd_buffer, message_size );
                new_data = true;
                state = 0;
            } else {
                Serial.print("failed check sum, id = "); Serial.print(message_id); Serial.print(" len = "); Serial.println(message_size);
                // check sum failure
                state = 0;
            }
        }
    }

    return new_data;
}


int write_packet(uint8_t packet_id, uint8_t *payload, int size) {
    // start of message sync bytes
    Serial1.write(START_OF_MSG0);
    Serial1.write(START_OF_MSG1);

    // packet id (1 byte)
    Serial1.write(packet_id);

    // packet length (1 byte)
    Serial1.write(size);

    // write payload
    Serial1.write( payload, size );

    // check sum (2 bytes)
    byte cksum0, cksum1;
    ugear_cksum( packet_id, size, payload, size, &cksum0, &cksum1 );
    Serial1.write(cksum0);
    Serial1.write(cksum1);
    
    return size + 6;
}

/* output an acknowledgement of a message received */
int write_ack_bin( uint8_t command_id, uint8_t subcommand_id )
{
    ack_packet_t payload;
    byte size = sizeof(payload);

    payload.command_id = command_id;
    payload.subcommand_id = subcommand_id;

    return write_packet( ACK_PACKET_ID, (uint8_t *)&payload, size);
}


/* output a binary representation of the pilot (rc receiver) data */
int write_pilot_in_bin()
{
    pilot_packet_t payload;
    byte size = sizeof(payload);

    // receiver data
    for ( int i = 0; i < SBUS_CHANNELS; i++ ) {
        payload.channel[i] = receiver_norm[i] * 16384.0;
    }

    // flags
    payload.flags = receiver_flags;

    return write_packet( PILOT_PACKET_ID, (uint8_t *)&payload, size);
}

void write_pilot_in_ascii()
{
    // pilot (receiver) input data
    if ( receiver_flags & SBUS_FAILSAFE ) {
        Serial.print("FAILSAFE! ");
    }
    if ( receiver_norm[0] < 0 ) {
        Serial.print("(Manual) ");
    } else {
        Serial.print("(Auto) ");
    }
    if ( receiver_norm[1] < 0 ) {
        Serial.print("(Throttle safety) ");
    } else {
        Serial.print("(Throttle enable) ");
    }
    for ( int i = 0; i < 7; i++ ) {
        Serial.print(receiver_norm[i], 3);
        Serial.print(" ");
    }
    Serial.println();
}

void write_actuator_out_ascii()
{
    // actuator output
    Serial.print("RCOUT:");
    for ( int i = 0; i < PWM_CHANNELS; i++ ) {
        Serial.print(actuator_pwm[i]);
        Serial.print(" ");
    }
    Serial.println();
}

/* output a binary representation of the IMU data (note: scaled to 16bit values) */
int write_imu_bin()
{
    imu_packet_t payload;
    byte size = sizeof(payload);
    
    payload.micros = imu_micros;

    for ( int i = 0; i < 10; i++ ) {
        payload.channel[i] = imu_packed[i];
    }
    
    return write_packet( IMU_PACKET_ID, (uint8_t *)&payload, size );
}

void write_imu_ascii()
{
    // output imu data
    Serial.print("IMU: ");
    Serial.print(imu_micros); Serial.print(" ");
    for ( int i = 0; i < 10; i++ ) {
        Serial.print(imu_calib[i], 3); Serial.print(" ");
    }
    Serial.println();
}

/* output a binary representation of the GPS data */
int write_gps_bin()
{
    byte size = sizeof(gps_data);

    if ( !new_gps_data ) {
        return 0;
    } else {
        new_gps_data = false;
    }
    
    return write_packet( GPS_PACKET_ID, (uint8_t *)&gps_data, size );
}

void write_gps_ascii() {
    Serial.print("GPS:");
    Serial.print(" Lat:");
    Serial.print((double)gps_data.lat / 10000000.0, 7);
    //Serial.print(gps_data.lat);
    Serial.print(" Lon:");
    Serial.print((double)gps_data.lon / 10000000.0, 7);
    //Serial.print(gps_data.lon);
    Serial.print(" Alt:");
    Serial.print((float)gps_data.hMSL / 1000.0);
    Serial.print(" Vel:");
    Serial.print(gps_data.velN / 1000.0);
    Serial.print(", ");
    Serial.print(gps_data.velE / 1000.0);
    Serial.print(", ");
    Serial.print(gps_data.velD / 1000.0);
    Serial.print(" GSP:");
    Serial.print(gps_data.gSpeed, DEC);
    Serial.print(" COG:");
    Serial.print(gps_data.heading, DEC);
    Serial.print(" SAT:");
    Serial.print(gps_data.numSV, DEC);
    Serial.print(" FIX:");
    Serial.print(gps_data.fixType, DEC);
    Serial.print(" TIM:");
    Serial.print(gps_data.hour); Serial.print(':');
    Serial.print(gps_data.min); Serial.print(':');
    Serial.print(gps_data.sec);
    Serial.print(" DATE:");
    Serial.print(gps_data.month); Serial.print('/');
    Serial.print(gps_data.day); Serial.print('/');
    Serial.print(gps_data.year);
    Serial.println();
}

/* output a binary representation of the barometer data */
int write_airdata_bin()
{
    airdata_packet_t payload;
    byte size = sizeof(payload);

    payload.baro_press_pa = baro_press;
    payload.baro_temp_C = baro_temp;
    payload.baro_hum = baro_hum;
    payload.ext_diff_press_pa = airdata_diffPress_pa;
    payload.ext_static_press_pa = airdata_staticPress_pa;
    payload.ext_temp_C = airdata_temp_C;
      
    return write_packet( AIRDATA_PACKET_ID, (uint8_t *)&payload, size );
}

void write_airdata_ascii()
{
    Serial.print("Barometer: ");
    Serial.print(baro_press, 2); Serial.print(" (st pa) ");
    Serial.print(baro_temp, 2); Serial.print(" (C) ");
    Serial.print(baro_hum, 1); Serial.print(" (%RH) ");
    Serial.print("AMS: ");
    Serial.print(airdata_staticPress_pa, 4); Serial.print(" (st pa) ");
    Serial.print(airdata_diffPress_pa, 4); Serial.print(" (diff pa) ");
    Serial.print(airdata_temp_C, 2); Serial.print(" (C) ");
    Serial.print(airdata_error_count); Serial.print(" (errors) ");
    Serial.println();
}

/* output a binary representation of various volt/amp sensors */
int write_power_bin()
{
    power_packet_t payload;
    byte size = sizeof(payload);

    payload.int_main_v = (uint16_t)(pwr1_v*100);
    payload.avionics_v = (uint16_t)(avionics_v*100);
    payload.ext_main_v = (uint16_t)(pwr2_v*100);
    payload.ext_main_amp = (uint16_t)(pwr_a*100);
 
    return write_packet( POWER_PACKET_ID, (uint8_t *)&payload, size );
}

void write_power_ascii()
{
    // This info is static so we don't need to send it at a high rate ... once every 10 seconds (?)
    // with an immediate message at the start.
    Serial.print("SN: ");
    Serial.println(read_serial_number());
    Serial.print("Firmware: ");
    Serial.println(FIRMWARE_REV);
    Serial.print("Main loop hz: ");
    Serial.println( MASTER_HZ);
    Serial.print("Baud: ");Serial.println(DEFAULT_BAUD);
    Serial.print("Main v: "); Serial.print(pwr1_v, 2);
    Serial.print(" av: "); Serial.println(avionics_v, 2);
}

/* output a binary representation of various status and config information */
int write_status_info_bin()
{
    static uint32_t write_millis = millis();

    status_packet_t payload;
    byte size = sizeof(payload);

    // This info is static or slow changing so we don't need to send
    // it at a high rate.
    static int counter = 0;
    if ( counter > 0 ) {
        counter--;
        return 0;
    } else {
        counter = MASTER_HZ * 1 - 1; // a message every 1 seconds (-1 so we aren't off by one frame) 
    }

    payload.serial_number = (uint16_t)serial_number;
    payload.firmware_rev = (uint16_t)FIRMWARE_REV;
    payload.master_hz = (uint16_t)MASTER_HZ;
    payload.baud = (uint32_t)DEFAULT_BAUD;

    // estimate sensor output byte rate
    unsigned long current_time = millis();
    unsigned long elapsed_millis = current_time - write_millis;
    unsigned long byte_rate = output_counter * 1000 / elapsed_millis;
    write_millis = current_time;
    output_counter = 0;
    payload.byte_rate = (uint16_t)byte_rate;
 
    return write_packet( STATUS_INFO_PACKET_ID, (uint8_t *)&payload, size );
}

void write_status_info_ascii()
{
    // This info is static so we don't need to send it at a high rate ... once every 10 seconds (?)
    // with an immediate message at the start.
    Serial.print("SN: ");
    Serial.println(read_serial_number());
    Serial.print("Firmware: ");
    Serial.println(FIRMWARE_REV);
    Serial.print("Main loop hz: ");
    Serial.println( MASTER_HZ);
    Serial.print("Baud: ");Serial.println(DEFAULT_BAUD);
}
