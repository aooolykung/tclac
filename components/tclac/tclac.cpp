#include "esphome.h"
#include "esphome/core/defines.h"
#include "tclac.h"

namespace esphome{
namespace tclac{


ClimateTraits tclacClimate::traits() {
	auto traits = climate::ClimateTraits();

	traits.set_supports_action(false);

	traits.set_supported_modes(
	{
		climate::CLIMATE_MODE_AUTO,
		climate::CLIMATE_MODE_COOL,
		climate::CLIMATE_MODE_DRY,
		climate::CLIMATE_MODE_FAN_ONLY,
		climate::CLIMATE_MODE_HEAT,
		climate::CLIMATE_MODE_OFF
	});

	traits.set_supported_fan_modes(
	{
		climate::CLIMATE_FAN_AUTO,			//	auto
		climate::CLIMATE_FAN_QUIET,			//	silent
		climate::CLIMATE_FAN_LOW,			//	|
		climate::CLIMATE_FAN_MIDDLE,		//	||
		climate::CLIMATE_FAN_MEDIUM,		//	|||
		climate::CLIMATE_FAN_HIGH,			//	||||
		climate::CLIMATE_FAN_FOCUS,			//	|||||
		climate::CLIMATE_FAN_DIFFUSE		//	POWER [7]
	});

	//traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF,climate::CLIMATE_SWING_BOTH,climate::CLIMATE_SWING_VERTICAL,climate::CLIMATE_SWING_HORIZONTAL});
	traits.set_supported_swing_modes(this->supported_swing_modes_);
	
	if (!traits.get_supported_swing_modes().empty())
		traits.add_supported_swing_mode(ClimateSwingMode::CLIMATE_SWING_OFF);

	traits.set_visual_min_temperature(MIN_SET_TEMPERATURE);
	traits.set_visual_max_temperature(MAX_SET_TEMPERATURE);
	traits.set_visual_temperature_step(STEP_TEMPERATURE);
	traits.set_supports_current_temperature(true);
	traits.set_supports_two_point_target_temperature(false);

	return traits;
}


void tclacClimate::setup() {

//	Serial.begin(9600);
//	ESP_LOGD("TCL" , "Started Serial");
#ifdef CONF_RX_LED
	this->rx_led_pin_->setup();
	this->rx_led_pin_->digital_write(false);
#endif
#ifdef CONF_TX_LED
	this->tx_led_pin_->setup();
	this->tx_led_pin_->digital_write(false);
#endif
}

void tclacClimate::loop()  {
	// Если в буфере UART что-то есть, то читаем это что-то
	if (esphome::uart::UARTDevice::available() > 0) {
		dataShow(0, true);
		dataRX[0] = esphome::uart::UARTDevice::read();
		// Если принятый байт- не заголовок (0xBB), то просто покидаем цикл
		if (dataRX[0] != 0xBB) {
			ESP_LOGD("TCL", "Wrong byte");
			dataShow(0,0);
			return;
		}
		// А вот если совпал заголовок (0xBB), то начинаем чтение по цепочке еще 4 байт
		delay(5);
		dataRX[1] = esphome::uart::UARTDevice::read();
		delay(5);
		dataRX[2] = esphome::uart::UARTDevice::read();
		delay(5);
		dataRX[3] = esphome::uart::UARTDevice::read();
		delay(5);
		dataRX[4] = esphome::uart::UARTDevice::read();

		auto raw = getHex(dataRX, 5);
		ESP_LOGD("TCL", "first 5 byte : %s ", raw.c_str());

		//Из первых 5 байт нам нужен пятый- он содержит длину сообщения
		esphome::uart::UARTDevice::read_array(dataRX+5, dataRX[4]+1);

		byte check = getChecksum(dataRX, sizeof(dataRX));

		raw = getHex(dataRX, sizeof(dataRX));
		ESP_LOGD("TCL", "RX full : %s ", raw.c_str());
		// Проверяем контрольную сумму
		if (check != dataRX[60]) {
			ESP_LOGD("TCL", "Invalid checksum %x", check);
			tclacClimate::dataShow(0,0);
			return;
		} else {
			ESP_LOGD("TCL", "checksum OK %x", check);
		}
		tclacClimate::dataShow(0,0);
		// Прочитав все из буфера приступаем к разбору данных
		tclacClimate::readData();
	}
}

void tclacClimate::update() {
	
	tclacClimate::dataShow(1,1);
	//Serial.write(poll, sizeof(poll));
	this->esphome::uart::UARTDevice::write_array(poll, sizeof(poll));
	auto raw = tclacClimate::getHex(poll, sizeof(poll));
	ESP_LOGD("TCL", "chek status sended");
	tclacClimate::dataShow(1,0);
}

void tclacClimate::readData() {
	
	current_temperature = float((( (dataRX[17] << 8) | dataRX[18] ) / 374 - 32)/1.8);
	target_temperature = (dataRX[FAN_SPEED_POS] & SET_TEMP_MASK) + 16;

	ESP_LOGD("TCL", "TEMP: %f ", current_temperature);

	if (dataRX[MODE_POS] & ( 1 << 4)) {
		//Если кондиционер включен, то разбираем данные для отображения
		uint8_t modeswitch = MODE_MASK & dataRX[MODE_POS];
		uint8_t fanspeedswitch = FAN_SPEED_MASK & dataRX[FAN_SPEED_POS];
		uint8_t swingmodeswitch = SWING_MODE_MASK & dataRX[SWING_POS];

		switch (modeswitch) {
			case MODE_AUTO:
				mode = climate::CLIMATE_MODE_AUTO;
				break;
			case MODE_COOL:
				mode = climate::CLIMATE_MODE_COOL;
				break;
			case MODE_DRY:
				mode = climate::CLIMATE_MODE_DRY;
				break;
			case MODE_FAN_ONLY:
				mode = climate::CLIMATE_MODE_FAN_ONLY;
				break;
			case MODE_HEAT:
				mode = climate::CLIMATE_MODE_HEAT;
				break;
			default:
				mode = climate::CLIMATE_MODE_AUTO;
		}

		if ( dataRX[FAN_QUIET_POS] & FAN_QUIET) {
			fan_mode = climate::CLIMATE_FAN_QUIET;
		} else if (dataRX[MODE_POS] & FAN_DIFFUSE){
			fan_mode = climate::CLIMATE_FAN_DIFFUSE;
		} else {
			switch (fanspeedswitch) {
				case FAN_AUTO:
					fan_mode = climate::CLIMATE_FAN_AUTO;
					break;
				case FAN_LOW:
					fan_mode = climate::CLIMATE_FAN_LOW;
					break;
				case FAN_MIDDLE:
					fan_mode = climate::CLIMATE_FAN_MIDDLE;
					break;
				case FAN_MEDIUM:
					fan_mode = climate::CLIMATE_FAN_MEDIUM;
					break;
				case FAN_HIGH:
					fan_mode = climate::CLIMATE_FAN_HIGH;
					break;
				case FAN_FOCUS:
					fan_mode = climate::CLIMATE_FAN_FOCUS;
					break;
				default:
					fan_mode = climate::CLIMATE_FAN_AUTO;
			}
		}

		switch (swingmodeswitch) {
			case SWING_OFF: 
				swing_mode = climate::CLIMATE_SWING_OFF;
				break;
			case SWING_HORIZONTAL:
				swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
				break;
			case SWING_VERTICAL:
				swing_mode = climate::CLIMATE_SWING_VERTICAL;
				break;
			case SWING_BOTH:
				swing_mode = climate::CLIMATE_SWING_BOTH;
				break;
		}
	} else {
		// Если кондиционер выключен, то все режимы показываются, как выключенные
		mode = climate::CLIMATE_MODE_OFF;
		fan_mode = climate::CLIMATE_FAN_OFF;
		swing_mode = climate::CLIMATE_SWING_OFF;
	}
	// Публикуем данные
	this->publish_state();
    }

// Climate control
void tclacClimate::control(const ClimateCall &call) {
	
	uint8_t switchvar = 0;
	
	dataTX[7]  = 0b00000000;//eco,display,beep,ontimerenable, offtimerenable,power,0,0
	dataTX[8]  = 0b00000000;//mute,0,turbo,health,mode(4)  0=cool 1=fan  2=dry 3=heat 4=auto 
	dataTX[9]  = 0b00000000;//[9] = 0,0,0,0,temp(4) 31 - value
	dataTX[10] = 0b00000000;//[10] = 0,timerindicator,swingv(3),fan(3) 0=auto 1=low 2=med 3=high
	//																{0,2,3,5,0};
	dataTX[11] = 0b00000000;
	dataTX[32] = 0b00000000;
	dataTX[33] = 0b00000000;
	
	if (call.get_mode().has_value()){
		switchvar = call.get_mode().value();
	} else {
		switchvar = mode;
	}

	// Включаем или отключаем пищалку в зависимости от переключателя в настройках
	if (beeper_status_){
		ESP_LOGD("TCL", "Beep mode ON");
		dataTX[7] += 0b00100000;
	} else {
		ESP_LOGD("TCL", "Beep mode OFF");
		dataTX[7] += 0b00000000;
	}
	
	// Включаем или отключаем дисплей на кондиционере в зависимости от переключателя в настройках
	// Включаем дисплей только если кондиционер в одном из рабочих режимов
	
	// ВНИМАНИЕ! При выключении дисплея кондиционер сам принудительно переходит в автоматический режим!
	
	if ((display_status_) && (switchvar != climate::CLIMATE_MODE_OFF)){
		ESP_LOGD("TCL", "Dispaly turn ON");
		dataTX[7] += 0b01000000;
	} else {
		ESP_LOGD("TCL", "Dispaly turn OFF");
		dataTX[7] += 0b00000000;
	}
		
	// Настраиваем режим работы кондиционера
	switch (switchvar) {
		case climate::CLIMATE_MODE_OFF:
			dataTX[7] += 0b00000000;
			dataTX[8] += 0b00000000;
			break;
		case climate::CLIMATE_MODE_AUTO:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00001000;
			break;
		case climate::CLIMATE_MODE_COOL:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000011;	
			break;
		case climate::CLIMATE_MODE_DRY:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000010;	
			break;
		case climate::CLIMATE_MODE_FAN_ONLY:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000111;	
			break;
		case climate::CLIMATE_MODE_HEAT:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000001;	
			break;
	}

	// Настраиваем режим вентилятора
	if (call.get_fan_mode().has_value()){
		switchvar = call.get_fan_mode().value();
		switch(switchvar) {
			case climate::CLIMATE_FAN_AUTO:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000000;
				break;
			case climate::CLIMATE_FAN_QUIET:
				dataTX[8]	+= 0b10000000;
				dataTX[10]	+= 0b00000000;
				break;
			case climate::CLIMATE_FAN_LOW:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000001;
				break;
			case climate::CLIMATE_FAN_MIDDLE:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000110;
				break;
			case climate::CLIMATE_FAN_MEDIUM:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000011;
				break;
			case climate::CLIMATE_FAN_HIGH:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000111;
				break;
			case climate::CLIMATE_FAN_FOCUS:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000101;
				break;
			case climate::CLIMATE_FAN_DIFFUSE:
				dataTX[8]	+= 0b01000000;
				dataTX[10]	+= 0b00000000;
				break;
		}		
	} else {
		if(fan_mode == climate::CLIMATE_FAN_AUTO){
			dataTX[8]	+= 0b00000000;
			dataTX[10]	+= 0b00000000;
		} else if(fan_mode == climate::CLIMATE_FAN_QUIET){
			dataTX[8]	+= 0b10000000;
			dataTX[10]	+= 0b00000000;
		} else if(fan_mode == climate::CLIMATE_FAN_LOW){
			dataTX[8]	+= 0b00000000;
			dataTX[10]	+= 0b00000001;
		} else if(fan_mode == climate::CLIMATE_FAN_MIDDLE){
			dataTX[8]	+= 0b00000000;
			dataTX[10]	+= 0b00000110;
		} else if(fan_mode == climate::CLIMATE_FAN_MEDIUM){
			dataTX[8]	+= 0b00000000;
			dataTX[10]	+= 0b00000011;
		} else if(fan_mode == climate::CLIMATE_FAN_HIGH){
			dataTX[8]	+= 0b00000000;
			dataTX[10]	+= 0b00000111;
		} else if(fan_mode == climate::CLIMATE_FAN_FOCUS){
			dataTX[8]	+= 0b00000000;
			dataTX[10]	+= 0b00000101;
		} else if(fan_mode == climate::CLIMATE_FAN_DIFFUSE){
			dataTX[8]	+= 0b01000000;
			dataTX[10]	+= 0b00000000;
		}
	}

        //Режим заслонок
		//	Вертикальная заслонка
		//		Качание вертикальной заслонки [10 байт, маска 00111000]:
		//			000 - Качание отключено, заслонка в последней позиции или в фиксации
		//			111 - Качание включено в выбранном режиме
		//		Режим качания вертикальной заслонки (режим фиксации заслонки роли не играет, если качание включено) [32 байт, маска 00011000]:
		//			01 - качание сверху вниз, ПО УМОЛЧАНИЮ
		//			10 - качание в верхней половине
		//			11 - качание в нижней половине
		//		Режим фиксации заслонки (режим качания заслонки роли не играет, если качание выключено) [32 байт, маска 00000111]:
		//			000 - нет фиксации, ПО УМОЛЧАНИЮ
		//			001 - фиксация вверху
		//			010 - фиксация между верхом и серединой
		//			011 - фиксация в середине
		//			100 - фиксация между серединой и низом
		//			101 - фиксация внизу
		//	Горизонтальные заслонки
		//		Качание горизонтальных заслонок [11 байт, маска 00001000]:
		//			0 - Качание отключено, заслонки в последней позиции или в фиксации
		//			1 - Качание включено в выбранном режиме
		//		Режим качания горизонтальных заслонок (режим фиксации заслонок роли не играет, если качание включено) [33 байт, маска 00111000]:
		//			001 - качание слева направо, ПО УМОЛЧАНИЮ
		//			010 - качание слева
		//			011 - качание по середине
		//			100 - качание справа
		//		Режим фиксации горизонтальных заслонок (режим качания заслонок роли не играет, если качание выключено) [33 байт, маска 00000111]:
		//			000 - нет фиксации, ПО УМОЛЧАНИЮ
		//			001 - фиксация слева
		//			010 - фиксация между левой стороной и серединой
		//			011 - фиксация в середине
		//			100 - фиксация между серединой и правой стороной
		//			101 - фиксация справа
		
		
	//Запрашиваем данные из переключателя режимов качания заслонок
	if (call.get_swing_mode().has_value()){
		switchvar = call.get_swing_mode().value();
	} else {
		// А если в переключателе пусто- заполняем значением из последнего опроса состояния. Типа, ничего не поменялось.
		switchvar = swing_mode;
	}
	
	switch(switchvar) {
		case climate::CLIMATE_SWING_OFF:
			dataTX[10]	+= 0b00000000;
			dataTX[11]	+= 0b00000000;
			break;
		case climate::CLIMATE_SWING_VERTICAL:
			dataTX[10]	+= 0b00111000;
			dataTX[11]	+= 0b00000000;
			break;
		case climate::CLIMATE_SWING_HORIZONTAL:
			dataTX[10]	+= 0b00000000;
			dataTX[11]	+= 0b00001000;
			break;
		case climate::CLIMATE_SWING_BOTH:
			dataTX[10]	+= 0b00111000;
			dataTX[11]	+= 0b00001000;  
			break;
	}
	//Выбираем режим для качания вертикальной заслонки
	switch(vertical_swing_direction_) {
		case VerticalSwingDirection::UP_DOWN:
			dataTX[32]	+= 0b00001000;
			ESP_LOGD("TCL", "Vertical swing: up-down");
			break;
		case VerticalSwingDirection::UPSIDE:
			dataTX[32]	+= 0b00010000;
			ESP_LOGD("TCL", "Vertical swing: upper");
			break;
		case VerticalSwingDirection::DOWNSIDE:
			dataTX[32]	+= 0b00011000;
			ESP_LOGD("TCL", "Vertical swing: downer");
			break;
	}
	//Выбираем режим для качания горизонтальных заслонок
	switch(horizontal_swing_direction_) {
		case HorizontalSwingDirection::LEFT_RIGHT:
			dataTX[33]	+= 0b00001000;
			ESP_LOGD("TCL", "Horizontal swing: left-right");
			break;
		case HorizontalSwingDirection::LEFTSIDE:
			dataTX[33]	+= 0b00010000;
			ESP_LOGD("TCL", "Horizontal swing: lefter");
			break;
		case HorizontalSwingDirection::CENTER:
			dataTX[33]	+= 0b00011000;
			ESP_LOGD("TCL", "Horizontal swing: center");
			break;
		case HorizontalSwingDirection::RIGHTSIDE:
			dataTX[33]	+= 0b00100000;
			ESP_LOGD("TCL", "Horizontal swing: righter");
			break;
	}
	//Выбираем положение фиксации вертикальной заслонки
	switch(this->vertical_direction_) {
		case AirflowVerticalDirection::LAST:
			dataTX[32]	+= 0b00000000;
			ESP_LOGD("TCL", "Vertical fix: last position");
			break;
		case AirflowVerticalDirection::MAX_UP:
			dataTX[32]	+= 0b00000001;
			ESP_LOGD("TCL", "Vertical fix: up");
			break;
		case AirflowVerticalDirection::UP:
			dataTX[32]	+= 0b00000010;
			ESP_LOGD("TCL", "Vertical fix: upper");
			break;
		case AirflowVerticalDirection::CENTER:
			dataTX[32]	+= 0b00000011;
			ESP_LOGD("TCL", "Vertical fix: center");
			break;
		case AirflowVerticalDirection::DOWN:
			dataTX[32]	+= 0b00000100;
			ESP_LOGD("TCL", "Vertical fix: downer");
			break;
		case AirflowVerticalDirection::MAX_DOWN:
			dataTX[32]	+= 0b00000101;
			ESP_LOGD("TCL", "Vertical fix: down");
			break;
	}
	//Выбираем положение фиксации горизонтальных заслонок
	switch(this->horizontal_direction_) {
		case AirflowHorizontalDirection::LAST:
			dataTX[33]	+= 0b00000000;
			ESP_LOGD("TCL", "Horizontal fix: last position");
			break;
		case AirflowHorizontalDirection::MAX_LEFT:
			dataTX[33]	+= 0b00000001;
			ESP_LOGD("TCL", "Horizontal fix: left");
			break;
		case AirflowHorizontalDirection::LEFT:
			dataTX[33]	+= 0b00000010;
			ESP_LOGD("TCL", "Horizontal fix: lefter");
			break;
		case AirflowHorizontalDirection::CENTER:
			dataTX[33]	+= 0b00000011;
			ESP_LOGD("TCL", "Horizontal fix: center");
			break;
		case AirflowHorizontalDirection::RIGHT:
			dataTX[33]	+= 0b00000100;
			ESP_LOGD("TCL", "Horizontal fix: righter");
			break;
		case AirflowHorizontalDirection::MAX_RIGHT:
			dataTX[33]	+= 0b00000101;
			ESP_LOGD("TCL", "Horizontal fix: right");
			break;
	}

	// Расчет и установка температуры
	if (call.get_target_temperature().has_value()) {
		dataTX[9] = 31-(int)call.get_target_temperature().value();		//0,0,0,0, temp(4)
	} else {
		dataTX[9] = 31-(int)target_temperature;
	}

	//Собираем массив байт для отправки в кондиционер
	dataTX[0] = 0xBB;	//стартовый байт заголовка
	dataTX[1] = 0x00;	//стартовый байт заголовка
	dataTX[2] = 0x01;	//стартовый байт заголовка
	dataTX[3] = 0x03;	//0x03 - управление, 0x04 - опрос
	dataTX[4] = 0x20;	//0x20 - управление, 0x19 - опрос
	dataTX[5] = 0x03;	//??
	dataTX[6] = 0x01;	//??
	//dataTX[7] = 0x64;	//eco,display,beep,ontimerenable, offtimerenable,power,0,0
	//dataTX[8] = 0x08;	//mute,0,turbo,health, mode(4) mode 01 heat, 02 dry, 03 cool, 07 fan, 08 auto, health(+16), 41=turbo-heat 43=turbo-cool (turbo = 0x40+ 0x01..0x08)
	//dataTX[9] = 0x0f;	//0 -31 ;    15 - 16 0,0,0,0, temp(4) settemp 31 - x
	//dataTX[10] = 0x00;	//0,timerindicator,swingv(3),fan(3) fan+swing modes //0=auto 1=low 2=med 3=high
	//dataTX[11] = 0x00;	//0,offtimer(6),0
	dataTX[12] = 0x00;	//fahrenheit,ontimer(6),0 cf 80=f 0=c
	dataTX[13] = 0x01;	//??
	dataTX[14] = 0x00;	//0,0,halfdegree,0,0,0,0,0
	dataTX[15] = 0x00;	//??
	dataTX[16] = 0x00;	//??
	dataTX[17] = 0x00;	//??
	dataTX[18] = 0x00;	//??
	dataTX[19] = 0x00;	//sleep on = 1 off=0
	dataTX[20] = 0x00;	//??
	dataTX[21] = 0x00;	//??
	dataTX[22] = 0x00;	//??
	dataTX[23] = 0x00;	//??
	dataTX[24] = 0x00;	//??
	dataTX[25] = 0x00;	//??
	dataTX[26] = 0x00;	//??
	dataTX[27] = 0x00;	//??
	dataTX[28] = 0x00;	//??
	dataTX[30] = 0x00;	//??
	dataTX[31] = 0x00;	//??
	//dataTX[32] = 0x00;	//0,0,0,режим вертикального качания(2),режим вертикальной фиксации(3)
	//dataTX[33] = 0x00;	//0,0,режим горизонтального качания(3),режим горизонтальной фиксации(3)
	dataTX[34] = 0x00;	//??
	dataTX[35] = 0x00;	//??
	dataTX[36] = 0x00;	//??
	dataTX[37] = 0xFF;	//Контрольная сумма
	dataTX[37] = tclacClimate::getChecksum(dataTX, sizeof(dataTX));

	tclacClimate::sendData(dataTX, sizeof(dataTX));
}

void tclacClimate::sendData(byte * message, byte size) {
	tclacClimate::dataShow(1,1);
	//Serial.write(message, size);
	this->esphome::uart::UARTDevice::write_array(message, size);
	auto raw = getHex(message, size);
	ESP_LOGD("TCL", "Message to TCL sended...");
	tclacClimate::dataShow(1,0);
}

String tclacClimate::getHex(byte *message, byte size) {
	String raw;
	for (int i = 0; i < size; i++) {
		raw += "\n" + String(message[i]);
	}
	raw.toUpperCase();
	return raw;
}

byte tclacClimate::getChecksum(const byte * message, size_t size) {
	byte position = size - 1;
	byte crc = 0;
	for (int i = 0; i < position; i++)
		crc ^= message[i];
	return crc;
}

void tclacClimate::dataShow(bool flow, bool shine) {
	if (module_display_status_){
		if (flow == 0){
			if (shine == 1){
#ifdef CONF_RX_LED
				this->rx_led_pin_->digital_write(true);
#endif
			} else {
#ifdef CONF_RX_LED
				this->rx_led_pin_->digital_write(false);
#endif
			}
		}
		if (flow == 1) {
			if (shine == 1){
#ifdef CONF_TX_LED
				this->tx_led_pin_->digital_write(true);
#endif
			} else {
#ifdef CONF_TX_LED
				this->tx_led_pin_->digital_write(false);
#endif
			}
		}
	}
}

// Действия с данными из конфига

//void tclacClimate::set_supported_modes(const std::set<climate::ClimateMode> &modes) {
//  this->traits.set_supported_modes(modes);
//  this->traits.add_supported_mode(climate::CLIMATE_MODE_OFF);        // Always available
//  this->traits.add_supported_mode(climate::CLIMATE_MODE_HEAT_COOL);  // Always available
//}

void tclacClimate::set_beeper_state(bool state) {
	this->beeper_status_ = state;
}

void tclacClimate::set_display_state(bool state) {
	this->display_status_ = state;
}

#ifdef CONF_RX_LED
void tclacClimate::set_rx_led_pin(GPIOPin *rx_led_pin) {
	this->rx_led_pin_ = rx_led_pin;
}
#endif

#ifdef CONF_TX_LED
void tclacClimate::set_tx_led_pin(GPIOPin *tx_led_pin) {
	this->tx_led_pin_ = tx_led_pin;
}
#endif

void tclacClimate::set_module_display_state(bool state) {
	this->module_display_status_ = state;
}

void tclacClimate::set_vertical_airflow(AirflowVerticalDirection direction) {
	this->vertical_direction_ = direction;
}

void tclacClimate::set_horizontal_airflow(AirflowHorizontalDirection direction) {
	this->horizontal_direction_ = direction;
}

void tclacClimate::set_vertical_swing_direction(VerticalSwingDirection direction) {
	this->vertical_swing_direction_ = direction;
}

void tclacClimate::set_horizontal_swing_direction(HorizontalSwingDirection direction) {
	this->horizontal_swing_direction_ = direction;
}

void tclacClimate::set_supported_swing_modes(const std::set<climate::ClimateSwingMode> &modes) {
	this->supported_swing_modes_ = modes;
}


// Заготовки функций запроса состояния, может пригодиться в будущем, если делать обратную связь. Очень не хочется, будет очень костыльно.

//bool tclacClimate::get_beeper_state() const { return this->beeper_status_; }
//bool tclacClimate::get_display_state() const { return this->display_status_; }
//bool tclacClimate::get_module_display_state() const { return this->module_display_status_; }
//AirflowVerticalDirection tclacClimate::get_vertical_airflow() const { return this->vertical_direction_; };
//AirflowHorizontalDirection tclacClimate::get_horizontal_airflow() const { return this->horizontal_direction_; }
//VerticalSwingDirection tclacClimate::get_vertical_swing_direction() const { return this->vertical_swing_direction_; }
//HorizontalSwingDirection tclacClimate::get_horizontal_swing_direction() const { return this->horizontal_swing_direction_; }



}
}