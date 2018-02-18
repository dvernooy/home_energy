// this is the header file that tells the compiler what pins and ports, etc.
// are available on this chip.


//changed THRESHOLD1 from 90 to 30 as laser seems to be losing juice 10/1/16
 #include <avr/io.h>

 #define STROBE PB4
 #define ADC_IN PB3

 // Some macros that make the code more readable
 #define output_low(port,pin) port &= ~(1<<pin)
 #define output_high(port,pin) port |= (1<<pin)
 #define set_input(portdir,pin) portdir &= ~(1<<pin)
 #define set_output(portdir,pin) portdir |= (1<<pin) 

 #define DUTY_CYCLE 50 //full range of TCNT0 is 0-255
 
 uint16_t result;
 uint8_t output_level, rampup_counter, rampdown_counter, itsup, itsdown;
 double signal_average, background_average, count_signal, count_background, THRESHOLD1, THRESHOLD2;
 
void pwm_setup(uint16_t duty){

	// Initial TIMER1 Phase and Frequency Correct PWM
	// Set the Timer/Counter Control Register
	TCCR0A = (1<<COM0A1) |(1<<WGM01)|(1<<WGM00); // Set OC1A when up counting, Clear when down counting
	TCCR0B = (0<<CS00) |(0<<CS01)|(1<<CS02)|(0<<WGM02);; // Phase/Freq-correct PWM, top value = OCRA, Prescaler: Off    
    OCR0A = duty;   
	// Enable PORTB0 
	DDRB |= (1<<PB0); 
}

void pwm_duty(uint16_t duty){
   OCR0A = duty;

}

void delay_ms(uint16_t ms) { //'kills time' with calibration
 uint16_t delay_count = 80;
   volatile uint16_t i; 
   while (ms != 0) {
     for (i=0; i != delay_count; i++);
     ms--;
   }
 }

/* INIT ADC */
void adc_init(void)
{
	/** Setup and enable ADC **/
	ADMUX = (0<<REFS2)|
			(0<<REFS1)|  	// Reference Selection Bits
			(0<<REFS0)|		// VCC is reference
			(0<<ADLAR)|		// ADC Left Adjust Result
			(0<<MUX3)|		// ANalog Channel Selection Bits
			(0<<MUX2)|		// ANalog Channel Selection Bits
			(1<<MUX1)|		// ADC3 (PB3)
			(1<<MUX0);
	
	ADCSRA = (1<<ADEN)|	// ADC ENable
			(0<<ADSC)|		// ADC Start Conversion
			(0<<ADATE)|		// ADC Auto Trigger Enable
			(0<<ADIF)|		// ADC Interrupt Flag
			(0<<ADIE)|		// ADC Interrupt Enable
			(0<<ADPS2)|		// ADC Prescaler Select Bits
			(0<<ADPS1)|
			(1<<ADPS0);
}

/* READ ADC PINS */
uint16_t read_adc(void) {
		ADCSRA |= (1<<ADSC);
		while(ADCSRA & (1<<ADSC));
		result = ADC;
    return result;
}

 
 int main(void) {
   // initialize pwm
   pwm_setup(DUTY_CYCLE);
   
   // initialize the direction of PORTB #4 to be an output
   set_output(DDRB, STROBE); 
   output_low(PORTB, STROBE); 
   
   // initialize the direction of PORTB #3 to be an input
   set_input(DDRB, ADC_IN);  
 
   adc_init();
   pwm_duty(DUTY_CYCLE);
   
   THRESHOLD1 = 30;//5V/1024 bits approx 5mV avg per bit
   //measuring 330 mV, so use half of it
   output_level = 0;
   rampup_counter = 0;
   rampdown_counter = 0;
   output_low(PORTB, STROBE); 
   

   while (1) {
     signal_average = 0.0;
	 background_average = 0.0;
	 count_signal = 0.0;
	 count_background = 0.0;
    while (TCNT0 < 245) {  
     // read ADC
	 read_adc();
		if ((TCNT0 > 8) && (TCNT0 < (DUTY_CYCLE + 4))) {
			count_signal++;
			signal_average = signal_average + (double) result;
		}
		else {
		   if (TCNT0 > (DUTY_CYCLE + 15)) {
			count_background++;
			background_average = background_average + (double) result;
			}
		}
    }
	signal_average = signal_average/count_signal;
	background_average = background_average/count_background;
	   
	   if ((itsdown == 1) && (output_level == 0) && (signal_average > (background_average + THRESHOLD1))){
	   itsdown =0;
	   rampup_counter=0;
	   }
	 
	   if ((itsup == 1) && (output_level == 1) && (signal_average < (background_average + THRESHOLD1))) {
	   itsup =0;
	   rampdown_counter=0;
	   }
	 
	 
	 //if ADC above a certain value, strobe 
	  if ((signal_average < (background_average + THRESHOLD1)) && (output_level == 0)) {
		rampup_counter++;
		itsdown = 1;
			if (rampup_counter > 15) {
				output_high(PORTB, STROBE);
				output_level = 1;
				rampup_counter = 0;
				itsdown = 0;
				}
	}	
	 else {
		if ((signal_average > (background_average + THRESHOLD1)) && (output_level == 1)) {
		rampdown_counter++;
		itsup = 1;
			if (rampdown_counter > 15) {
				output_low(PORTB, STROBE);
				output_level =0;
				rampdown_counter = 0;
				itsup = 0;
			}
        }
	}
   } //while(1)
} //main













