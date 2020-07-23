/* servo-tester.c
 *
 * Hobby RC servo tester:
 *  1. RC servo PWN generator
 *  2. 'Center' and 'Manual' input switch
 *  3. Analog position input command for manual servo slew
 *
 * When switch is in 'Center' mode, PWM is preset to move servo to
 * center position (1.5mSec pulse width).
 * When the switch is set to 'Manual' position the tester reads
 * the analog input (potentiometer) and sets the servo PWN
 *  to the requested position (between 1.0 mSec and 2.0 mSec).
 *
 *  +-----+
 *  |     |
 *  | AVR +-------> (OC1A) Servo PWM
 *  |     |
 *  |     +-------< (PA1)  Center/Manual switch
 *  |     |
 *  |     +-------< (ADC0) Position potentiometer
 *  |     |
 *  +-----+
 *
 * Port A bit assignment
 *
 *  b7 b6 b5 b4 b3 b2 b1 b0
 *  |  |  |  |  |  |  |  |
 *  |  |  |  |  |  |  |  +--- 'i' ADC0
 *  |  |  |  |  |  |  +------ 'i' Center/Manual (w/ pull-up)
 *  |  |  |  |  |  +--------- 'i' n/a
 *  |  |  |  |  +------------ 'i' n/a
 *  |  |  |  +--------------- 'i' n/a
 *  |  |  +------------------ 'i' n/a
 *  |  +--------------------- 'o' OC1A Servo PWM output
 *  +------------------------ 'i' n/a
 *
 * Port B bit assignment
 *
 *             b3 b2 b1 b0
 *             |  |  |  |
 *             |  |  |  +--- 'i' n/a
 *             |  |  +------ 'i' n/a
 *             |  +--------- 'i' n/a
 *             +------------ 'i' ^Reset
 *
 * note: all references to data sheet are for ATtiny84 Rev. 8006K–AVR–10/10
 * 
 */

#include    <stdint.h>

#include    <avr/io.h>
#include    <avr/interrupt.h>
#include    <avr/wdt.h>
#include    <util/delay.h>

// IO ports initialization
#define     PA_DDR_INIT     0b01000000  // port data direction
#define     PA_PUP_INIT     0b00000010  // port input pin pull-up
#define     PA_INIT         0x00        // port initial values

#define     PB_DDR_INIT     0b00000000  // port data direction
#define     PB_PUP_INIT     0b00000000  // port input pin pull-up
#define     PB_INIT         0x00        // port initial values

// Timer1 initialization
#define     TIM1_CTRLA      0b10000010  // Clear on compare
#define     TIM1_CTRLB      0b00011011  // Mode=14

// ADC initialization
#define     ADMUX_INIT      0b00000000  // Vcc reference, ADC0 input
#define     ADCSRA_INIT     0b11101111  // Enable conversion, auto trigger, @ 62.6KHz
#define     ADCSRB_INIT     0b00010000  // Free running, result is left shifted into ADCH

// Timer1 constants for PWM
#define     SERVO_PERIOD    2499        // 20mSec PWM period with Clock Select to Fclk/64

#define     PWM_INIT        PWM_CENTER
#define     PWM_LOW         123
#define     PWM_CENTER      184
#define     PWM_HIGH        246

#define     PWM_RANGE       (PWM_HIGH-PWM_LOW)  // ** never 'zero' **

// Center-Manual switch position
#define     SET_CENTER      1
#define     SET_MANUAL      0

/****************************************************************************
  special function prototypes
****************************************************************************/
// This function is called upon a HARDWARE RESET:
void reset(void) __attribute__((naked)) __attribute__((section(".init3")));

/****************************************************************************
  Globals
****************************************************************************/
volatile uint8_t analog_readout = 0;

/* ----------------------------------------------------------------------------
 * ioinit()
 *
 *  Initialize IO interfaces
 *  Timer and data rates calculated based on internal oscillator
 *
 */
void ioinit(void)
{
    // Reconfigure system clock scaler to 8MHz
    CLKPR = 0x80;   // change clock scaler (sec 6.5.2 p.31)
    CLKPR = 0x00;

    // Initialize Timer1 for servo PWM (see sec 11.9.3 Fast PWM Mode)
    // 20mSec pulse interval, with 1mSec to 2mSec variable pulse width
    // Fast PWN mode with Non-inverting Compare Output mode to clear output
    TCNT1  = 0;
    OCR1A  = PWM_INIT;
    ICR1   = SERVO_PERIOD;
    TCCR1A = TIM1_CTRLA;
    TCCR1B = TIM1_CTRLB;
    TCCR1C = 0;
    TIMSK1 = 0;

    // ADC initialization
    ADMUX  = ADMUX_INIT;
    ADCSRA = ADCSRA_INIT;
    ADCSRB = ADCSRB_INIT;

    // Initialize IO pins
    DDRA  = PA_DDR_INIT;            // PA pin directions
    PORTA = PA_INIT | PA_PUP_INIT;  // initial value and pull-up setting

    DDRB  = PB_DDR_INIT;            // PB pin directions
    PORTB = PB_INIT | PB_PUP_INIT;  // initial value and pull-up setting
}

/* ----------------------------------------------------------------------------
 * reset()
 *
 *  Clear SREG_I on hardware reset.
 *  source: http://electronics.stackexchange.com/questions/117288/watchdog-timer-issue-avr-atmega324pa
 */
void reset(void)
{
     cli();
    // Note that for newer devices (any AVR that has the option to also
    // generate WDT interrupts), the watchdog timer remains active even
    // after a system reset (except a power-on condition), using the fastest
    // prescaler value (approximately 15 ms). It is therefore required
    // to turn off the watchdog early during program startup.
    MCUSR = 0; // clear reset flags
    wdt_disable();
}

/* ----------------------------------------------------------------------------
 * This ISR will trigger when digital to analog conversion ends
 * and store the conversion value.
 *
 */
ISR(ADC_vect)
{
    analog_readout = ADCH;
}

/* ----------------------------------------------------------------------------
 * main() control functions
 *
 */
int main(void)
{
    float   temp;
    uint8_t center_manual_sw = SET_CENTER;
    uint8_t prev_sw_state = SET_MANUAL;

    // Initialize hardware
    ioinit();

    // Enable interrupts
    sei();

    // Center the servo and delay 2sec
    _delay_ms(2000);

    // Loop forever to sample Center/Manual switch,
    // and update servo PWM pulse width counter.
    while ( 1 )
    {
        center_manual_sw = ((PINA & 0b00000010) >> 1);

        if ( center_manual_sw != prev_sw_state && 
             center_manual_sw == SET_CENTER )
        {
            OCR1A = PWM_CENTER;
        }
        else if ( center_manual_sw == SET_MANUAL )
        {
            temp = (float)analog_readout;
            temp = temp * (PWM_RANGE / 255.0);
            temp = temp + PWM_LOW;
            OCR1A = (uint8_t)temp;
        }

        prev_sw_state = center_manual_sw;
    } /* endless while loop */

    return 0;
}
