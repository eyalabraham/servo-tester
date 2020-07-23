# Servo tester

Hobby RC servo tester:
1. RC servo PWN generator
2. 'Center' and 'Manual input switch
3. Analog position input command for manual servo slew

When switch is in 'Center' mode, PWM is preset to move servo to center position (1.5mSec pulse width). When the switch is set to 'Manual' position the tester reads the analog input (potentiometer) and sets the servo PWN to the requested position (between 1.0 mSec and 2.0 mSec).

More on this project on this web page [https://sites.google.com/site/eyalabraham/rc-servo-tester]

```
    +-----+
    |     |
    | AVR +-------> (OC1A) Servo PWM
    |     |
    |     +-------< (PA1)  Center/Manual switch
    |     |
    |     +-------< (ADC0) Position potentiometer
    |     |
    +-----+
```

## ATtiny84 AVR IO

| Function             | AVR  | Pin    | I/O       |
|----------------------|------|--------|-----------|
| Servo PWN out        | OC1A | pin 7  | out       |
| Center/Manual switch | PA1  | pin 12 | in        |
| Position pot         | ADC0 | pin 13 | analog in |

## Project files

- **servo-tester** - tester code

