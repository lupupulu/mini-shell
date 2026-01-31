#ifndef CONFIG_H
#define CONFIG_H

#include "mnsh.h"

key_setting_t key_setting[] = {
    { 0x01 , NULL , "to_start"        },
    { 0x02 , NULL , "to_start"        },
    { 0x06 , NULL , "to_end"          },
    { 0x08 , NULL , "backspace"       },
    { 0x0B , NULL , "clear_right"     },
    { 0x17 , NULL , "clear_last_word" },
    { 0x7F , NULL , "backspace"       },

    { 0x1B , "[3~"   , "delete"       },
    { 0x1B , "[C"    , "right"        },
    { 0x1B , "[D"    , "left"         },
    { 0x1B , "[A"    , "last_history" },
    { 0x1B , "[B"    , "next_history" },
    { 0x1B , "[C"    , "right"        },
    { 0x1B , "[H"    , "to_start"     },
    { 0x1B , "[F"    , "to_end"       },
    { 0x1B , "[1;5D" , "last_word"    },
    { 0x1B , "[1;5C" , "next_word"    },
};

#endif