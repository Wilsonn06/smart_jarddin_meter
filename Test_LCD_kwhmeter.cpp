#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>

//Pin RX dan TX Serial2
int rxPin = 16;
int txPin = 17;

PZEM004Tv30 pzem(Serial2, rxPin, txPin);

// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);


void setup() 
{
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, rxPin, txPin);
    lcd.init();

    lcd.begin(16, 2);
    // Turn on the backlight
    lcd.backlight();
  
    // Print a message to the LCD.
    lcd.print("Hello, world!");
    pinMode(26, OUTPUT);
    pinMode(27, OUTPUT);
    digitalWrite(26, HIGH);
    digitalWrite(27, HIGH);
}

void loop() 
{
    float voltage = pzem.voltage();
    float current = pzem.current();
    float power = pzem.power();
    float energy = pzem.energy();
    float frequency = pzem.frequency();
    float pf = pzem.pf();
    
    Serial.print("Voltage: ");
    Serial.print(voltage);
    Serial.print("V, Current: ");
    Serial.print(current);
    Serial.print("A, Power: "); //Power = Voltage x Current x PF
    Serial.print(power);
    Serial.print("W, Energy: ");
    Serial.print(energy); //Energy = Power x Time (1 second) + Previous Energy

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Energy: ");
    lcd.print(energy);
    lcd.print("Wh");
    lcd.setCursor(0, 1);
    lcd.print("Power: ");
    lcd.print(power);
    lcd.print("W");

    Serial.print("Wh, Frequency: ");
    Serial.print(frequency);
    Serial.print("Hz, PF: ");
    Serial.print(pf);

    delay(1000);

}