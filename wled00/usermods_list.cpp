// usermods_list.cpp — registers whichever word clock usermod is being built.
// The correct define is set by the PlatformIO environment in
// platformio_override.ini.  Only one is compiled per build.

#ifdef USERMOD_ID_WORDCLOCK
  #include "../usermods/WordClock/wordclock_usermod.h"
#endif

#ifdef USERMOD_ID_WORDCLOCK_8X8
  #include "../usermods/WordClock8x8/wordclock_8x8_usermod.h"
#endif

void registerUsermods()
{
  #ifdef USERMOD_ID_WORDCLOCK
    UsermodManager::add(new WordClockUsermod());
  #endif

  #ifdef USERMOD_ID_WORDCLOCK_8X8
    UsermodManager::add(new WordClock8x8Usermod());
  #endif
}
