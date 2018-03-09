#ifndef __OSHOW_H__
#define __OSHOW_H__
/*
 *  Copyright (c) 2017, Jisoo Yang
 *
 *  This is written as part of CS 370 project
 *
 *  Writen by Jisoo Yang <jisoo.yang@unlv.edu>
 */

// messages from client
#define QUOTE_ME_FLOOR (1)
#define QUOTE_ME_MEZZANINE (2)
#define QUOTE_ME_BALCONY (3)
#define QUOTE_ME_BOX (4)
#define I_ACCEPT (11)
#define I_REJECT (12)


// special values from server
#define TICKET_SOLD_OUT (-1)
#define REJECT_ACKNOWLEDGED (-2)


#endif
