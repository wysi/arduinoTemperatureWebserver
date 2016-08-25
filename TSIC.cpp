/**
* Library for reading TSIC digital temperature sensors like 305 and 206
* using the Arduino platform.
*
* Copyright: Rolf Wagner
* Date: March 9th 2014
*
* Version 2
*		Improvements:
*		- Arduino > 1.0 compatible
*		- corrected offset (about +2°C)
*		- code run time optimization
*		- no freezing of system in case sensor gets unplugged
*		- measure strobetime instead of constant waiting time (-> high temperature stable)
*		- code cleanup
*		- space optimization, now only 239 byte
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
* http://playground.arduino.cc/Code/Tsic
*/

#include "Arduino.h"
#include "TSIC.h"

// Init der Aus/Eingänge
TSIC::TSIC(uint8_t signal_pin, uint8_t vcc_pin)
	: m_signal_pin(signal_pin), m_vcc_pin(vcc_pin) 
{
    pinMode(m_vcc_pin, OUTPUT);
    pinMode(m_signal_pin, INPUT);
}

// auslesen der Temperatur
uint8_t TSIC::getTemperture(uint16_t *temp_value16){
		uint16_t temp_value1 = 0;
		uint16_t temp_value2 = 0;

		TSIC_ON();
		delayMicroseconds(50);     // wait for stabilization
		if(TSIC::readSens(&temp_value1)){}			// 1. Byte einlesen
		else return 0;
		if(TSIC::readSens(&temp_value2)){}			// 2. Byte einlesen
		else return 0;
		if(checkParity(&temp_value1)){}		// Parity vom 1. Byte prüfen
		else return 0;
		if(checkParity(&temp_value2)){}		// Parity vom 2. Byte prüfen
		else return 0;

		TSIC_OFF();		// Sensor ausschalten
		*temp_value16 = (temp_value1 << 8) + temp_value2;
		return 1;
}

//-------------Unterprogramme-----------------------------------------------------------------------

/*	Konvertieren der Temperatur aus unsinged Integer in °C mit einer Nachkommastelle
	Die Umrechnung ist geschwindigkeitsoptimiert. Dadurch wird die Genauigkeit minimal verschlechtert (@25°C um -0,0366°C).
*/
float TSIC::calc_Celsius(uint16_t *temperature16){
	uint16_t temp_value16 = 0;
	unsigned long temp_buffer = 0;
	float celsius = 0;
	//temp_value16 = ((*temperature16 * 250L) >> 8) - 500;			// Temperatur *10 also 26,4 = 264
	//celsius = temp_value16 / 10 + (float) (temp_value16 % 10) / 10;	// Temperatur mit 1 Nachkommastelle z.b. 26,4°C
	temp_buffer = *temperature16;
	celsius =  ((float) temp_buffer * 70 / 2047) - 10;
	return celsius;
}

uint8_t TSIC::readSens(uint16_t *temp_value){
	uint16_t strobelength = 0;
	uint16_t strobetemp = 0;
	uint8_t dummy = 0;
	uint16_t timeout = 0;
	while (TSIC_HIGH){	// wait until start bit starts
		timeout++;
		delayMicroseconds(5);	// sonst kommt es zum Abbruch
		Abbruch();
	}
	// Measure strobe time
	strobelength = 0;
	timeout = 0;
	while (TSIC_LOW) {    // wait for rising edge
		strobelength++;
		timeout++;
		delayMicroseconds(5);
		Abbruch();
	}
	for (uint8_t i=0; i<9; i++) {
		// Wait for bit start
		timeout = 0;
		while (TSIC_HIGH) { // wait for falling edge
			timeout++;
			Abbruch();
		}
		// Delay strobe length
		timeout = 0;
		dummy = 0;
		strobetemp = strobelength;
		while (strobetemp--) {
			timeout++;
			dummy++;
			delayMicroseconds(5);	// sonst kommt es zum Abbruch
			Abbruch();
		}
		*temp_value <<= 1;
		// Read bit
		if (TSIC_HIGH) {
			*temp_value |= 1;
		}
		// Wait for bit end
		timeout = 0;
		while (TSIC_LOW) {		// wait for rising edge
			timeout++;
			Abbruch();
		}
	}
	return 1;
}

uint8_t TSIC::checkParity(uint16_t *temp_value) {
	uint8_t parity = 0;

	for (uint8_t i = 0; i < 9; i++) {
		if (*temp_value & (1 << i))
			parity++;
	}
	if (parity % 2)
		return 0;				// Parityfehler
	*temp_value >>= 1;          // Parity Bit löschen
	return 1;
}