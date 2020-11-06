int dutyCycle = 0;
int frequency = 100;
int dac = 0;

const byte numChars = 32;
char receivedChars[numChars];
boolean newData = false;

void setup() {
    // setup code, runs once, on start
    // basic setup for serial connection and output pin
    Serial.begin(9600);
    pinMode(A0, OUTPUT);
    analogWriteResolution(10);
    analogWrite(A0, 0);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    //starting setup for the timer interrupts
    //Code is a slightly modified/combined version of the code from the following examples:
    //https://shawnhymel.com/1710/arduino-zero-samd21-raw-pwm-using-cmsis/ and
    //https://github.com/nebs/arduino-zero-timer-demo/blob/master/src/main.cpp
    // Enable and configure generic clock generator 4
    GCLK->GENCTRL.reg = GCLK_GENCTRL_IDC |          // Improve duty cycle
                        GCLK_GENCTRL_GENEN |        // Enable generic clock gen
                        GCLK_GENCTRL_SRC_DFLL48M |  // Select 48MHz as source
                        GCLK_GENCTRL_ID(4);         // Select GCLK4
    while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization
    
    // Set clock divider of 1 to generic clock generator 4
    GCLK->GENDIV.reg = GCLK_GENDIV_DIV(1) |         // Divide 48 MHz by 1
                       GCLK_GENDIV_ID(4);           // Apply to GCLK4 4
    while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization
    
    // Enable GCLK4 and connect it to TCC0 and TCC1
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN |        // Enable generic clock
                        GCLK_CLKCTRL_GEN_GCLK4 |    // Select GCLK4
                        GCLK_CLKCTRL_ID_TCC0_TCC1;  // Feed GCLK4 to TCC0/1
    while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

    // Divide counter by 64, giving 750 kHz (1.3µs ns on each TCC0 tick)
    TCC0->CTRLA.reg |= TCC_CTRLA_PRESCALER(TCC_CTRLA_PRESCALER_DIV64_Val);

    // Use Normal frequency (NFRQ) Waveform output: count up to PER, match on CC[n]
    TCC0->WAVE.reg = TCC_WAVE_WAVEGEN_NFRQ;         // Select NFRQ as waveform
    while (TCC0->SYNCBUSY.bit.WAVE);                // Wait for synchronization

    // Set the period (the number to count to (TOP) before resetting timer), the initial period in this case corresponds to the maximum frequency
    TCC0->PER.reg = 499;
    while (TCC0->SYNCBUSY.bit.PER);

    // For the start set PWM signal to output 0% duty cycle
    // n for CC[n] is determined by n = x % 4 where x is from WO[x]
    TCC0->CC[1].reg = 4;
    while (TCC0->SYNCBUSY.bit.CC1);

    TCC0->INTENSET.reg = 0;              // disable all interrupts
    TCC0->INTENSET.bit.OVF = 1;          // enable overflow
    TCC0->INTENSET.bit.MC1 = 1;          // enable compare match to CC1

    // Enable InterruptVector
    NVIC_EnableIRQ(TCC0_IRQn);
  
    // Enable output (start PWM)
    TCC0->CTRLA.reg |= (TCC_CTRLA_ENABLE);
    while (TCC0->SYNCBUSY.bit.ENABLE);              // Wait for synchronization
}

void loop() {
    // look for new data on serial connection
    recvWithEndMarker();
    if (newData == true){
        char* commands;
        int commandNr;
        int receivedDac;
        int receivedCycle;
        int receivedFreq;

        //we have new data, parse the numbers in the command string we received
        commands = strtok(receivedChars, " ");
        commandNr = atoi(commands);
        commands = strtok(NULL, " ");
        receivedDac = atoi(commands);
        if (receivedDac < 0 || receivedDac > 100){
            Serial.print(commandNr);
            Serial.println(" err Invalid Dac value");
            newData = false;
            return;
        }
        commands = strtok(NULL, " ");
        receivedCycle = atoi(commands);
        if (receivedCycle < 0 || receivedCycle > 100){
            Serial.print(commandNr);
            Serial.println(" err Invalid Duty Cycle");
            newData = false;
            return;
        }
        commands = strtok(NULL, " ");
        receivedFreq = atoi(commands);
        if (receivedFreq < 0 || receivedFreq > 100){
            Serial.print(commandNr);
            Serial.println(" err Invalid Frequency");
            newData = false;
            return;
        }

        // dutyCycle is in percent, so a number [0,100] is fine
        dutyCycle = receivedCycle;
        // for frequency we have to map [0,100] to [14999,499], which are the top values needed for the range [50Hz, 1500Hz]
        // the top values can be calculated using the following formula:
        // top_val = (clock_frequency / (prescaler * target_frequency)) - 1
        // so with the established clock frequency 48MHz and the prescaler 64, we get:
        // top_val = (750kHz / target_frequency) - 1
        frequency = receivedFreq * -145 + 14999;
        //Serial.println(frequency);
        //Serial.println(frequency * ((float) dutyCycle / 100.0));

        // for dac we have to map [0,100] to [0,1023)
        dac = (float)receivedDac*3.2;

        //set the new TOP value
        TCC0->PER.reg = frequency;
        while (TCC0->SYNCBUSY.bit.PER);

        //set the new count value
        //TCC0->CC[1].reg = frequency - (frequency * ((float) dutyCycle / 100.0));
        TCC0->COUNT.reg = 0;
        TCC0->CC[1].reg = (int)(frequency * ((float) dutyCycle / 100.0));
        while (TCC0->SYNCBUSY.bit.CC1);

        // send response
        Serial.print(commandNr);
        Serial.println(" success");

        newData = false;
    }
}

/**
 * reads a single line of serial input into receivedChars, and sets newData to true, if there is any
 * 
 * the arduino internal method for this did not work well, as it was slow and left the line ending character in the incoming stream 
 */
void recvWithEndMarker() {
    
    static byte ndx = 0;
    char endMarker = '\n';
    char rc;
   
    while (Serial.available() > 0 && newData == false) {
        rc = Serial.read();

        if (rc != endMarker) {
            receivedChars[ndx] = rc;
            ndx++;
            if (ndx >= numChars) {
                ndx = numChars - 1;
            }
        }
        else {
            receivedChars[ndx] = '\0'; // terminate the string
            ndx = 0;
            newData = true;
        }
    }
}

/**
 * handles the interrupts from TCC0 and controls the pwm
 */
void TCC0_Handler()
{
    Tcc* TC = (Tcc*) TCC0;       // get timer struct
    if (TC->INTFLAG.bit.OVF == 1) {  // An overflow caused the interrupt --> start a new pwm period
        if(dac > 0) {
            analogWrite(A0, dac);
            digitalWrite(LED_BUILTIN, HIGH);
        }
        TC->INTFLAG.bit.OVF = 1;    // writing a one clears the flag ovf flag
    } 
    if (TC->INTFLAG.bit.MC1 == 1) {  // A compare to cc1 caused the interrupt --> start the low part of the pwm period
        analogWrite(A0, 0);
        digitalWrite(LED_BUILTIN, LOW);
        TC->INTFLAG.bit.MC1 = 1;    // writing a one clears the flag mc1 flag
    }
}
