/*
 * @file debounce_conf.h
 * @brief Compile-time configuration for the debounce library.
 *
 * @details
 *    Users may override any option below by defining the macro before
 *    including this header, or via a compiler flag (e.g.
 *    -DDEBOUNCE_ENABLE_CALLBACKS=1).  The #ifndef guards ensure that
 *    user-supplied definitions take precedence.
 */

#ifndef DEBOUNCE_CONF_H_
#define DEBOUNCE_CONF_H_

/*
 * DEBOUNCE_ENABLE_CALLBACKS
 *   Set to 1 to include the transition-callback mechanism.  When 0
 *   (the default), no function-pointer field is added to struct debounce
 *   and no callback-dispatch code is compiled.
 */
#ifndef DEBOUNCE_ENABLE_CALLBACKS
#define DEBOUNCE_ENABLE_CALLBACKS 0
#endif

#endif /* DEBOUNCE_CONF_H_ */
