#define MAX_HEIGHT 75000

#define READY 'r'
#define BUSY 'b'

#define END_COMMAND '\r'

#define INIT_ELEVATOR 'r'

#define CENTRAL_ELEVATOR 'c'
#define RIGHT_ELEVATOR 'd'
#define LEFT_ELEVATOR 'e'

#define UP 's'
#define STOP 'p'
#define DOWN 'd'

#define OPEN 'a'
#define CLOSED 'f'
#define SIGNAL_OPEN 'A'
#define SIGNAL_CLOSED 'F'
#define BUTTON_TRIGGER_FROM_INSIDE 'I'
#define BUTTON_TRIGGER_FROM_OUTSIDE 'E'

#define ON 'L'
#define OFF 'D'

#define FLOOR_0 'a'
#define FLOOR_1 'b'
#define FLOOR_2 'c'
#define FLOOR_3 'd'
#define FLOOR_4 'e'
#define FLOOR_5 'f'
#define FLOOR_6 'g'
#define FLOOR_7 'h'
#define FLOOR_8 'i'
#define FLOOR_9 'j'
#define FLOOR_10 'k'
#define FLOOR_11 'l'
#define FLOOR_12 'm'
#define FLOOR_13 'n'
#define FLOOR_14 'o'
#define FLOOR_15 'p'

typedef struct {                                // object data type
  char Command[10];
  int Size;
} MsgObj;