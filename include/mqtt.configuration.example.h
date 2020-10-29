/* 
  WaterTankController
  Control de Nivel de Tanqe de Agua con Sensor de Temperatura DS18B20.
  Reporte por MQTT a Home Assistant.

  Copyright (C) 2020  Guillermo Di Donato

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */

// Replace with your credentials

#define MQTT_USER "mqttuser" 
#define MQTT_PASSWORD "mqttpassword" 
#define MQTT_CLIENTID "WaterTankController"
#define MQTT_SERVER_IP 192, 168, 1, 10 
#define MQTT_SERVER_PORT 1883
#define MQTT_TOPIC  "WaterTankController/State"
