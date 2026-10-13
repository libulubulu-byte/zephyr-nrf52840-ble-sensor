/*
 * Board-level pins that are not application resources but still have to be
 * driven, because leaving them floating makes the board draw current.
 */

#ifndef BOARD_POWER_H_
#define BOARD_POWER_H_

/*
 * Drive the pins the board depends on into their non-drawing state.
 *
 * Called first thing in main(), before anything else touches a GPIO.
 */
void board_power_init(void);

#endif /* BOARD_POWER_H_ */
