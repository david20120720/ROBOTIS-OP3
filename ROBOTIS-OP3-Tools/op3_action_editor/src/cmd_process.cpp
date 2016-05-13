/*
 * cmd_process.cpp
 *
 *  Created on: 2013. 1. 23.
 *      Author: hjsong
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <term.h>
#include <fcntl.h>
#include <ncurses.h>
#include <pthread.h>
#include <sys/wait.h>

#include "op3_action_editor/cmd_process.h"

using namespace ROBOTIS;

ros::Publisher		enable_ctrl_module_pub;

int Col = STP7_COL;
int Row = ID_1_ROW;
int Old_Col;
int Old_Row;
bool bBeginCommandMode = false;
bool bEdited = false;
int indexPage = 1;

ACTION_FILE::PAGE Page;
ACTION_FILE::STEP Step;


RobotisController* _ctrl;
Robot* _op3_robot;
std::map<int, std::string> _id_to_name;


#define DOF_OF_ARM 3
int right_arm_id_list[DOF_OF_ARM] = { 1, 3, 5};
int left_arm_id_list[DOF_OF_ARM]  = { 2, 4, 6};

std::map<std::string, GroupSyncWrite *> port_to_sync_write_go_cmd;

int _getch()
{
	struct termios oldt, newt;
	int ch;
	tcgetattr( STDIN_FILENO, &oldt );
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr( STDIN_FILENO, TCSANOW, &newt );
	ch = getchar();
	tcsetattr( STDIN_FILENO, TCSANOW, &oldt );
	return ch;
}

int kbhit(void)
{
  struct termios oldt, newt;
  int ch;
  int oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if(ch != EOF)
  {
    ungetc(ch, stdin);
    return 1;
  }

  return 0;
}

struct termios oldterm, new_term;
void set_stdin(void)
{
	tcgetattr(0,&oldterm);
	new_term = oldterm;
	new_term.c_lflag &= ~(ICANON | ECHO | ISIG); // �ǹ̴� struct termios�� ã���� ��.
	new_term.c_cc[VMIN] = 1;
	new_term.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &new_term);
}

void reset_stdin(void)
{
	tcsetattr(0, TCSANOW, &oldterm);
}

bool AskSave()
{
	if(bEdited == true)
	{
		PrintCmd("Are you sure? (y/n)");
		if(_getch() != 'y')
		{
			ClearCmd();
			return true;
		}
	}

	return false;
}

void GoToCursor(int col, int row)
{
	char *cursor;
	char *esc_sequence;
	cursor = tigetstr("cup");
	esc_sequence = tparm(cursor, row, col);
	putp(esc_sequence);

	Col = col;
	Row = row;
}

void MoveUpCursor()
{
	if(Col >= STP7_COL && Col <= CCWSLOPE_COL)
	{
		if( Row > ID_1_ROW )
			GoToCursor(Col, Row-1);
	}
	else
	{
		if( Row > PLAYCOUNT_ROW )
			GoToCursor(Col, Row-1);
	}
}

void MoveDownCursor()
{
	if(Col >= STP7_COL && Col <= STP6_COL)
	{
		if( Row < SPEED_ROW )
			GoToCursor(Col, Row+1);
	}
	else if(Col <= CCWSLOPE_COL)
	{
		if( Row < ID_END_ROW )
			GoToCursor(Col, Row+1);
	}
	else
	{
		if( Row < EXIT_ROW )
			GoToCursor(Col, Row+1);
	}
}

void MoveLeftCursor()
{
	switch(Col)
	{
	case STP0_COL:
		GoToCursor(STP7_COL, Row);
		break;

	case STP1_COL:
		GoToCursor(STP0_COL, Row);
		break;

	case STP2_COL:
		GoToCursor(STP1_COL, Row);
		break;

	case STP3_COL:
		GoToCursor(STP2_COL, Row);
		break;

	case STP4_COL:
		GoToCursor(STP3_COL, Row);
		break;

	case STP5_COL:
		GoToCursor(STP4_COL, Row);
		break;

	case STP6_COL:
		GoToCursor(STP5_COL, Row);
		break;

	case CWSLOPE_COL:
		GoToCursor(STP6_COL, Row);
		break;

	case CCWSLOPE_COL:
		GoToCursor(CWSLOPE_COL, Row);
		break;

	case PAGEPARAM_COL:
		GoToCursor(CCWSLOPE_COL, Row);
		break;
	}
}

void MoveRightCursor()
{
	switch(Col)
	{
	case STP7_COL:
		GoToCursor(STP0_COL, Row);
		break;

	case STP0_COL:
		GoToCursor(STP1_COL, Row);
		break;

	case STP1_COL:
		GoToCursor(STP2_COL, Row);
		break;

	case STP2_COL:
		GoToCursor(STP3_COL, Row);
		break;

	case STP3_COL:
		GoToCursor(STP4_COL, Row);
		break;

	case STP4_COL:
		GoToCursor(STP5_COL, Row);
		break;

	case STP5_COL:
		GoToCursor(STP6_COL, Row);
		break;

	case STP6_COL:
		GoToCursor(CWSLOPE_COL, Row);
		break;

	case CWSLOPE_COL:
		GoToCursor(CCWSLOPE_COL, Row);
		break;

	case CCWSLOPE_COL:
		if(Row >= PLAYCOUNT_ROW && Row <= EXIT_ROW)
			GoToCursor(PAGEPARAM_COL, Row);
		break;
	}
}



bool InitializeActionEditor(std::string robot_file_path, std::string init_file_path, std::string offset_file_path)
{
	_ctrl = RobotisController::GetInstance();

	//Controller Initialize with robot file info
	if(_ctrl->Initialize(robot_file_path, init_file_path) == false) {
		ROS_ERROR("ROBOTIS Controller Initialize Fail!");
		return false;
	}
	_ctrl->LoadOffset(offset_file_path);
	_ctrl->AddMotionModule((MotionModule*)ActionModule::GetInstance());

	_op3_robot = _ctrl->robot;

	_id_to_name[1]   = "r_sho_pitch";
	_id_to_name[2]   = "l_sho_pitch";
	_id_to_name[3]   = "r_sho_roll";
	_id_to_name[4]   = "l_sho_roll";
	_id_to_name[5]   = "r_el";
	_id_to_name[6]   = "l_el";
	_id_to_name[7]   = "r_hip_yaw";
	_id_to_name[8]   = "l_hip_yaw";
	_id_to_name[9]   = "r_hip_roll";
	_id_to_name[10]  = "l_hip_roll";
	_id_to_name[11]  = "r_hip_pitch";
	_id_to_name[12]  = "l_hip_pitch";
	_id_to_name[13]  = "r_knee";
	_id_to_name[14]  = "l_knee";
	_id_to_name[15]  = "r_ank_pitch";
	_id_to_name[16]  = "l_ank_pitch";
	_id_to_name[17]  = "r_ank_roll";
	_id_to_name[18]  = "l_ank_roll";
	_id_to_name[19]  = "head_pan";
	_id_to_name[20]  = "head_tilt";

	ros::NodeHandle _nh;
	enable_ctrl_module_pub = _nh.advertise<std_msgs::String>("/robotis/enable_ctrl_module", 0);


	for(std::map<std::string, PortHandler *>::iterator _it = _op3_robot->ports.begin(); _it != _op3_robot->ports.end(); _it++)
	{
		port_to_sync_write_go_cmd[_it->first] = new GroupSyncWrite(_it->second,
				PacketHandler::GetPacketHandler(2.0),
				112,// "profile_velocity"
                8);
	}

	return true;
}



void ReadStep()
{
	UINT8_T  error = 0;
	UINT32_T value = 0;
	int id;
	bool Enable[NUM_OF_DXL + 1 ];

	for(int index = 0; index < NUM_OF_DXL + 1; index++)	{
		Enable[index] = false;
	}

	for(unsigned int jointIndex = 0; jointIndex < NUM_OF_DXL; jointIndex++)
	{
		id = jointIndex + 1;
		std::string _joint_name = _id_to_name[id];
		std::map<std::string, Dynamixel *>::iterator _dxls_it;

		_dxls_it = _op3_robot->dxls.find(_joint_name);
		if(_dxls_it != _op3_robot->dxls.end())
		{
			if(_ctrl->ReadCtrlItem(_joint_name, "torque_enable", &value, &error) == COMM_SUCCESS)
			{
				if(value == 1)
				{
					if(_ctrl->ReadCtrlItem(_joint_name, "present_position", &value, &error) == COMM_SUCCESS)
					{
						int _offset = _op3_robot->dxls[_joint_name]->ConvertRadian2Value(_op3_robot->dxls[_joint_name]->dxl_state->position_offset) - _op3_robot->dxls[_joint_name]->value_of_0_radian_position;
						value = value - _offset;
						Step.position[id] = value;

						//ROS_INFO_STREAM("R " << id << " : " << Step.position[id] <<"  " << value << " " << _offset);

						Enable[id] = true;
					}
					else {
						Step.position[id] = ACTION_FILE::INVALID_BIT_MASK;
					}
				}
				else {
					Step.position[id] = ACTION_FILE::INVALID_BIT_MASK;
				}
			}
			else {
				Step.position[id] = ACTION_FILE::INVALID_BIT_MASK;
			}

		}
		else {
			Step.position[id] = ACTION_FILE::INVALID_BIT_MASK;
		}

	}
}

void DrawIntro()
{
	int nrows, ncolumns;
    setupterm(NULL, fileno(stdout), (int *)0);
    nrows = tigetnum("lines");
    ncolumns = tigetnum("cols");

	system("clear");
	printf("\n");
	printf("[Action Editor for Thor %s]\n", PROGRAM_VERSION);
	printf("\n");
	printf(" *Terminal screen size must be %d(col)x%d(row).\n", SCREEN_COL, SCREEN_ROW);
    printf(" *Current terminal has %d columns and %d rows.\n", ncolumns, nrows);
	printf("\n");
	printf("\n");
	printf("Press any key to start program...\n");
	_getch();

	ActionModule::GetInstance()->LoadPage(indexPage, &Page);

	ReadStep();

	Step.pause = 0;
	Step.time = 0;

	DrawPage();
}

void DrawEnding()
{
	system("clear");
	printf("\n");
	printf("Terminate Action Editor");
	printf("\n");
}

void DrawPage()
{
	int old_col = Col;
	int old_row = Row;

	system("clear");
	// 80    01234567890123456789012345678901234567890123456789012345678901234567890123456789     //24
	printf( "ID: 1(R_SHO_PITCH)[    ]                                                       \n" );//0
	printf( "ID: 2(L_SHO_PITCH)[    ]                                       Page Number:    \n" );//1
	printf( "ID: 3(R_SHO_ROLL) [    ]                                        Address:       \n" );//2
	printf( "ID: 4(L_SHO_ROLL) [    ]                                         Play Count:   \n" );//3
	printf( "ID: 5(R_ELBOW)    [    ]                                          Page Step:   \n" );//4
	printf( "ID: 6(L_ELBOW)    [    ]                                         Page Speed:   \n" );//5
	printf( "ID: 7(R_HIP_YAW)  [    ]                                         Accel Time:   \n" );//6
	printf( "ID: 8(L_HIP_YAW)  [    ]                                       Link to Next:   \n" );//7
	printf( "ID: 9(R_HIP_ROLL) [    ]                                       Link to Exit:   \n" );//8
	printf( "ID:10(L_HIP_ROLL) [    ]                                                       \n" );//9
	printf( "ID:11(R_HIP_PITCH)[    ]                                                       \n" );//0
	printf( "ID:12(L_HIP_PITCH)[    ]                                                       \n" );//1
	printf( "ID:13(R_KNEE)     [    ]                                                       \n" );//2
	printf( "ID:14(L_KNEE)     [    ]                                                       \n" );//3
	printf( "ID:15(R_ANK_PITCH)[    ]                                                       \n" );//4
	printf( "ID:16(L_ANK_PITCH)[    ]                                                       \n" );//5
	printf( "ID:17(R_ANK_ROLL) [    ]                                                       \n" );//6
	printf( "ID:18(L_ANK_ROLL) [    ]                                                       \n" );//7
	printf( "ID:19(HEAD_PAN)   [    ]                                                       \n" );//8
	printf( "ID:20(HEAD_TILT)  [    ]                                                       \n" );//9
	printf( "   PauseTime      [    ]                                                       \n" );//9

	if( Page.header.schedule == ACTION_FILE::SPEED_BASE_SCHEDULE )
		printf( "   Speed          [    ]                                                       \n" );//1
	else if( Page.header.schedule == ACTION_FILE::TIME_BASE_SCHEDULE )
		printf( "   Time(x 8msec)  [    ]                                                       \n" );//1

	printf( "                   STP7  STP0 STP1 STP2 STP3 STP4 STP5 STP6                    \n" );//2
	printf( "]                                                                              " );  // 3

	for(int i=0; i<=ACTION_FILE::MAXNUM_STEP; i++ )
		DrawStep(i);

	int id;
	// Draw Compliance slope
	for( unsigned int index=0; index < NUM_OF_DXL; index++ )
	{
		id = index + 1;
		if(id <= NUM_OF_DXL) {
			GoToCursor(CWSLOPE_COL, id -1);
			printf( "%.1d%.1d", Page.header.pgain[id]>>4, Page.header.pgain[id]&0x0f );
		}
		else
			continue;
	}

	// Draw Page parameter
	GoToCursor( PAGEPARAM_COL, PLAYCOUNT_ROW );
	printf( "%.3d", Page.header.repeat );

	GoToCursor( PAGEPARAM_COL, STEPNUM_ROW );
	printf( "%.3d", Page.header.stepnum );

	GoToCursor( PAGEPARAM_COL, PLAYSPEED_ROW );
	printf( "%.3d", Page.header.speed );

	GoToCursor( PAGEPARAM_COL, ACCEL_ROW );
	printf( "%.3d", Page.header.accel );

	GoToCursor( PAGEPARAM_COL, NEXT_ROW );
	printf( "%.3d", Page.header.next );

	GoToCursor( PAGEPARAM_COL, EXIT_ROW );
	printf( "%.3d", Page.header.exit );

	// Draw Page information
	DrawName();

	GoToCursor(PAGENUM_COL, PAGENUM_ROW);
	printf( "%.4d", indexPage );

	GoToCursor(ADDR_COL, ADDR_ROW);
	printf( "0x%.5X", (int)(indexPage*sizeof(ACTION_FILE::PAGE)) );

	DrawStepLine(false);

	GoToCursor(old_col, old_row);
}

void DrawStep(int index)
{
	int old_col = Col;
	int old_row = Row;
	ACTION_FILE::STEP *step;
	int col;

	switch(index)
	{
	case 0:
		col = STP0_COL;
		step = &Page.step[0];
		break;

	case 1:
		col = STP1_COL;
		step = &Page.step[1];
		break;

	case 2:
		col = STP2_COL;
		step = &Page.step[2];
		break;

	case 3:
		col = STP3_COL;
		step = &Page.step[3];
		break;

	case 4:
		col = STP4_COL;
		step = &Page.step[4];
		break;

	case 5:
		col = STP5_COL;
		step = &Page.step[5];
		break;

	case 6:
		col = STP6_COL;
		step = &Page.step[6];
		break;

	case 7:
		col = STP7_COL;
		step = &Step;
		break;

	default:
		return;
	}

	int id;
	for(unsigned int index = 0; index<NUM_OF_DXL; index++ )
	{
		id = index + 1;
		if(id <= NUM_OF_DXL) {
			GoToCursor(col, id -1);
			if(step->position[id] & ACTION_FILE::INVALID_BIT_MASK) {
				printf("----");
			}
			else if(step->position[id] & ACTION_FILE::TORQUE_OFF_BIT_MASK)
				printf("????");
			else
				printf("%.4d", step->position[id]);
		}
		else
			continue;
	}

	GoToCursor(col, PAUSE_ROW);
	printf("%4.3d", step->pause);

	GoToCursor(col, SPEED_ROW);
	printf("%4.3d", step->time);

	GoToCursor( old_col, old_row );
}

void DrawStepLine(bool erase)
{
	int old_col = Col;
	int old_row = Row;
	int col;

	switch(Page.header.stepnum)
	{
	case 0:
		col = STP0_COL;
		break;

	case 1:
		col = STP1_COL;
		break;

	case 2:
		col = STP2_COL;
		break;

	case 3:
		col = STP3_COL;
		break;

	case 4:
		col = STP4_COL;
		break;

	case 5:
		col = STP5_COL;
		break;

	case 6:
		col = STP6_COL;
		break;

	case 7:
		col = CWSLOPE_COL;
		break;

	default:
		return;
	}
	col--;

	for( int index=1; index < NUM_OF_DXL+1; index++ )
	{
		GoToCursor(col, index -1);
		if(erase == true)
			printf( " " );
		else
			printf( "|" );
	}

	GoToCursor(old_col, old_row);
}

void DrawName()
{
	int old_col = Col;
	int old_row = Row;

	GoToCursor(NAME_COL, NAME_ROW);
	printf( "                " );
	GoToCursor(NAME_COL, NAME_ROW);

	for(int i=0; i<ACTION_FILE::MAXNUM_NAME; i++)
		printf("%c", (char)Page.header.name[i]);

	GoToCursor( old_col, old_row );
}

void ClearCmd()
{
	PrintCmd("                              ");
}

void PrintCmd(const char *message)
{
	int len = strlen(message);
	GoToCursor(0, CMD_ROW);

	printf( "] %s", message);
	for(int i=0; i<(SCREEN_COL - (len + 2)); i++)
		printf(" ");

	GoToCursor(len + 2, CMD_ROW);
}

void UpDownValue(int offset)
{
	SetValue(GetValue() + offset);
}

int GetValue()
{
	int col;
	int row;
	if(bBeginCommandMode == true)
	{
		col = Old_Col;
		row = Old_Row;
	}
	else
	{
		col = Col;
		row = Row;
	}

	if( col == STP7_COL )
	{
		if( row == PAUSE_ROW )
			return Step.pause;
		else if( row == SPEED_ROW )
			return Step.time;
		else
			return Step.position[row + 1];
	}
	else if( col <= STP6_COL )
	{
		int i = 0;
		switch(col)
		{
		case STP0_COL:
			i = 0;
			break;

		case STP1_COL:
			i = 1;
			break;

		case STP2_COL:
			i = 2;
			break;

		case STP3_COL:
			i = 3;
			break;

		case STP4_COL:
			i = 4;
			break;

		case STP5_COL:
			i = 5;
			break;

		case STP6_COL:
			i = 6;
			break;
		}

		if( row == PAUSE_ROW )
			return Page.step[i].pause;
		else if( row == SPEED_ROW )
			return Page.step[i].time;
		else
			return Page.step[i].position[row + 1];
	}
	else if(col == CWSLOPE_COL)
		return (Page.header.pgain[row + 1] >> 4);
	else if(col == CCWSLOPE_COL)
		return (Page.header.pgain[row + 1] & 0x0f);
	else if(col == PAGEPARAM_COL)
	{
		if(row == PLAYCOUNT_ROW)
			return Page.header.repeat;
		else if(row == STEPNUM_ROW)
			return Page.header.stepnum;
		else if(row == PLAYSPEED_ROW)
			return Page.header.speed;
		else if(row == ACCEL_ROW)
			return Page.header.accel;
		else if(row == NEXT_ROW)
			return Page.header.next;
		else if(row == EXIT_ROW)
			return Page.header.exit;
	}

	return -1;
}

void SetValue(int value)
{
	int col;
	int row;
	if(bBeginCommandMode == true)
	{
		col = Old_Col;
		row = Old_Row;
	}
	else
	{
		col = Col;
		row = Row;
	}

	GoToCursor(col, row);

	if( col == STP7_COL )
	{
		if( row == PAUSE_ROW )
		{
			if(value >= 0 && value <= 255)
			{
				Step.pause = value;
				printf( "%4.3d", value );
				bEdited = true;
			}
		}
		else if( row == SPEED_ROW )
		{
			if(value >= 0 && value <= 255)
			{
				Step.time = value;
				printf( "%4.3d", value );
				bEdited = true;
			}
		}
		else
		{
			if(value >= 0 && value <= 4095)
			{
				if(!(Step.position[row + 1] & ACTION_FILE::INVALID_BIT_MASK) && !(Step.position[row + 1] & ACTION_FILE::TORQUE_OFF_BIT_MASK))
				{
					UINT8_T error;
					UINT32_T ivalue;
					int id;
					for(unsigned int index = 0; index< NUM_OF_DXL ; index++)
					{
						id = index +1 ;
						if(id <= NUM_OF_DXL) {
							if(id == row + 1)
							{
								ivalue = (UINT32_T)value;
								std::string _joint_name = _id_to_name[id];
								if( _ctrl->WriteCtrlItem(_joint_name, "goal_position", ivalue, &error) == COMM_SUCCESS)
								{
									Step.position[row + 1] = value;
									printf( "%.4d", value );
									bEdited = true;
								}
								break;
							}
						}
					}
				}
			}
		}
	}
	else if( col <= STP6_COL )
	{
		int i = 0;
		switch(col)
		{
		case STP0_COL:
			i = 0;
			break;

		case STP1_COL:
			i = 1;
			break;

		case STP2_COL:
			i = 2;
			break;

		case STP3_COL:
			i = 3;
			break;

		case STP4_COL:
			i = 4;
			break;

		case STP5_COL:
			i = 5;
			break;

		case STP6_COL:
			i = 6;
			break;
		}

		if( row == PAUSE_ROW )
		{
			if(value >= 0 && value <= 255)
			{
				Page.step[i].pause = value;
				printf( "%4.3d", value );
				bEdited = true;
			}
		}
		else if( row == SPEED_ROW )
		{
			if(value >= 0 && value <= 255)
			{
				Page.step[i].time = value;
				printf( "%4.3d", value );
				bEdited = true;
			}
		}
		else
		{
			if(value >= 0 && value <= 4095)
			{
//				if(!(Page.step[i].position[row + 1] & Action::INVALID_BIT_MASK))
//				{
					Page.step[i].position[row + 1] = value;
					printf( "%.4d", value );
					bEdited = true;
//				}
			}
		}
	}
	else if(col == CWSLOPE_COL)
	{
		if(value >= 1 && value <= 7)
		{
			Page.header.pgain[row + 1] = (value << 4) + (Page.header.pgain[row + 1] & 0x0f);
			printf( "%.1d", value );
			bEdited = true;
		}
	}
	else if(col == CCWSLOPE_COL)
	{
		if(value >= 1 && value <= 7)
		{
			Page.header.pgain[row + 1] = (Page.header.pgain[row + 1] & 0xf0) + (value & 0x0f);
			printf( "%.1d", value );
			bEdited = true;
		}
	}
	else if(col == PAGEPARAM_COL)
	{
		if(row == PLAYCOUNT_ROW)
		{
			if(value >= 0 && value <= 255)
			{
				Page.header.repeat = value;
				printf( "%.3d", value );
				bEdited = true;
			}
		}
		else if(row == STEPNUM_ROW)
		{
			if(value >= 0 && value <= ACTION_FILE::MAXNUM_STEP)
			{
				if(Page.header.stepnum != value)
				{
					DrawStepLine(true);
					Page.header.stepnum = value;
					DrawStepLine(false);
					printf( "%.3d", value );
					bEdited = true;
				}
			}
		}
		else if(row == PLAYSPEED_ROW)
		{
			if(value >= 0 && value <= 255)
			{
				Page.header.speed = value;
				printf( "%.3d", value );
				bEdited = true;
			}
		}
		else if(row == ACCEL_ROW)
		{
			if(value >= 0 && value <= 255)
			{
				Page.header.accel = value;
				printf( "%.3d", value );
				bEdited = true;
			}
		}
		else if(row == NEXT_ROW)
		{
			if(value >= 0 && value <= 255)
			{
				Page.header.next = value;
				printf( "%.3d", value );
				bEdited = true;
			}
		}
		else if(row == EXIT_ROW)
		{
			if(value >= 0 && value <= 255)
			{
				Page.header.exit = value;
				printf( "%.3d", value );
				bEdited = true;
			}
		}
	}

	GoToCursor(col, row);

}

void ToggleTorque()
{
	if(Col != STP7_COL || Row > ID_END_ROW)
		return;

	int id = Row + 1;

	std::string _joint_name = _id_to_name[id];

	if(Step.position[id] & ACTION_FILE::TORQUE_OFF_BIT_MASK)
	{
		if(_ctrl->WriteCtrlItem(_joint_name, "torque_enable", 1, 0) != COMM_SUCCESS)
			return;

		UINT32_T value;
		if(_ctrl->ReadCtrlItem(_joint_name, "present_position", &value, 0) != COMM_SUCCESS)
			return;

		int _offset = _op3_robot->dxls[_joint_name]->ConvertRadian2Value(_op3_robot->dxls[_joint_name]->dxl_state->position_offset) - _op3_robot->dxls[_joint_name]->value_of_0_radian_position;
		value = value - _offset;


		Step.position[id] = value;
		printf("%.4d", value);
	}
	else
	{
		if(_ctrl->WriteCtrlItem(_joint_name, "torque_enable", 0, 0) != COMM_SUCCESS)
			return;

		Step.position[id] = ACTION_FILE::TORQUE_OFF_BIT_MASK;
		printf("????");
	}

	GoToCursor(Col, Row);
}

void BeginCommandMode()
{
	Old_Col = Col;
	Old_Row = Row;
	ClearCmd();
	GoToCursor(CMD_COL, CMD_ROW);
	bBeginCommandMode = true;
}

void EndCommandMode()
{
	GoToCursor(Old_Col, Old_Row);
	bBeginCommandMode = false;
}

void HelpCmd()
{
	system("clear");
	printf(" exit               Exits the program.\n");
	printf(" re                 Refreshes the screen.\n");
	printf(" b                  Move to previous page.\n");
	printf(" n                  Move to next page.\n");
	printf(" page [index]       Move to page [index].\n");
	printf(" list               View list of pages.\n");
	printf(" new                Clears data of current page and initializes page.\n");
	printf(" copy [index]       Copy data from page [index].\n");
	printf(" set [value]        Sets value on cursor [value].\n");
	printf(" save               Saves changes.\n");
	printf(" play               Motion playback of current page.\n");
    printf(" g [index]          Motion playback of STP[index].\n");
	printf(" name               Name for current page or changes the name of current page.\n");
	printf(" time               Change time base playing.\n");
	printf(" speed              Change speed base playing.\n");
	printf(" w [index]          Overwrites data from STP[index] with STP7.\n");
	printf(" i                  Inserts data from STP7 to STP0. \n" \
	       "                    Moves data from STP[x] to STP[x+1].\n");
	printf(" i [index]          Inserts data from STP7 to STP[index]. \n" \
	       "                    Moves data from STP[index] to STP[index+1].\n");
	printf(" m [index] [index2] Moves data from [index] to [index2] step.\n");
	printf(" d [index]          Deletes data from STP[index]. \n"
	       "                    Pushes data from STP[index] to STP[index-1].\n");
	printf(" on/off             Turn On/Off torque from ALL actuators.\n");
	printf(" on/off [index1] [index2] ...  \n"
	       "                    turns On/Off torque from ID[index1] ID[index2]...\n");
	printf("\n");
	printf("       Copyright ROBOTIS CO.,LTD.\n");
	printf("\n");
	printf(" Press any key to continue...");
	_getch();

	DrawPage();
}

void NextCmd()
{
	PageCmd(indexPage + 1);
}

void PrevCmd()
{
	PageCmd(indexPage - 1);
}

void PageCmd(int index)
{
	if(AskSave() == true)
		return;

	if(index > 0 && index < ACTION_FILE::MAXNUM_PAGE)
	{
		indexPage = index;
		ActionModule::GetInstance()->LoadPage(indexPage, &Page);

		Col = STP7_COL;
		Row = ID_1_ROW;
		DrawPage();
	}
	else
		PrintCmd("Invalid page index");

	bEdited = false;
}

void TimeCmd()
{
	Page.header.schedule = ACTION_FILE::TIME_BASE_SCHEDULE;
	bEdited = true;
	DrawPage();
}

void SpeedCmd()
{
	Page.header.schedule = ACTION_FILE::SPEED_BASE_SCHEDULE;
	bEdited = true;
	DrawPage();
}

void PlayCmd()
{
	UINT32_T value;

	for(int i=0; i<Page.header.stepnum; i++)
	{
//		for(int index=0; index<MotionStatus::m_CurrentJoints.size(); index++)
		for(int index=0; index < NUM_OF_DXL; index++)
		{
			int id = index + 1;
			if(Page.step[i].position[id] & ACTION_FILE::INVALID_BIT_MASK)
			{
				PrintCmd("Exist invalid joint value");
				return;
			}
		}
	}



	PrintCmd("Playing... ('s' to stop, 'b' to brake)");



    std_msgs::String _msg;
    _msg.data = "action_module";
	enable_ctrl_module_pub.publish(_msg);


	_ctrl->StartTimer();

	if(ActionModule::GetInstance()->Start(indexPage, &Page) == false)
	{
		PrintCmd("Failed to play this page!");
		_ctrl->StopTimer();
		return;
	}

	set_stdin();
	while(1)
	{
		if(ActionModule::GetInstance()->IsRunning() == false)
			break;

		if(kbhit())
		{
			int key = _getch();
			GoToCursor(CMD_COL, CMD_ROW);
			if(key == 's')
			{
				ActionModule::GetInstance()->Stop();
				fprintf(stderr, "\r] Stopping...                                  ");
			}
			else if(key == 'b')
			{
				ActionModule::GetInstance()->Brake();
				fprintf(stderr, "\r] Braking...                                   ");
			}
			else
				fprintf(stderr, "\r] Playing... ('s' to stop, 'b' to brake)");
		}

		usleep(10000);
	}
	reset_stdin();


	_ctrl->StopTimer();

	GoToCursor(CMD_COL, CMD_ROW);
	PrintCmd("Done.");

	usleep(10000);

	ReadStep();
	DrawStep(7);
}

void ListCmd()
{
	int old_col = Col;
	int old_row = Row;
	int index = 0;

	while(1)
	{
		system("clear");
		for(int i=0; i<22; i++)
		{
			for(int j=0; j<4; j++)
			{
				int k = (index * 88) + (j*22 + i);
				ACTION_FILE::PAGE page;
				if(ActionModule::GetInstance()->LoadPage(k, &page) == true)
				{
					printf(" |%.3d.", k);
					for(int n=0; n<ACTION_FILE::MAXNUM_NAME; n++)
					{
						if((char)page.header.name[n] >= ' ' && (char)page.header.name[n] <= '~')
							printf("%c", (char)page.header.name[n]);
						else
							printf(" ");
					}
				}
				else
				{
					printf(" |                ");
				}
			}
			printf("\n");
		}

		printf("\nAction Page List (%d/3) - Press key n(Next), b(Prev), q(Quit)", index + 1);
		while(1)
		{
			int ch = _getch();
			if(ch == 'n')
			{
				if(index < 2)
				{
					index++;
					break;
				}
			}
			else if(ch == 'b')
			{
				if(index > 0)
				{
					index--;
					break;
				}
			}
			else if(ch == 'q')
			{
				DrawPage();
				GoToCursor(old_col, old_row);
				return;
			}
		}
	}
}

void OnOffCmd( bool on, int num_param, int *list)
{
	UINT32_T _torque_enable  = 0;
	if(on)
		_torque_enable = 1;

	if(num_param == 0)
	{
		for(unsigned int index=0; index<NUM_OF_DXL; index++) {
			int id = index + 1;
			std::string _joint_name = _id_to_name[id];
			_ctrl->WriteCtrlItem(_joint_name,"torque_enable", _torque_enable,0);
		}
	}
	else
	{
		for(int i=0; i<num_param; i++)
		{
			if(list[i] >= 1 && list[i] < NUM_OF_DXL) {

				int id = list[i];
				std::string _joint_name = _id_to_name[id];
				_ctrl->WriteCtrlItem(_joint_name,"torque_enable", _torque_enable,0);
			}

		}
	}

	ReadStep();
	DrawStep(7);
}

void WriteStepCmd(int index)
{
	for(unsigned int jointIndex=0; jointIndex<NUM_OF_DXL; jointIndex++)
	{
		int id = jointIndex + 1;

		if(Step.position[id] & ACTION_FILE::TORQUE_OFF_BIT_MASK)
			return;

	}

	if(index >= 0 && index < ACTION_FILE::MAXNUM_STEP)
	{
		Page.step[index] = Step;
		DrawStep(index);
		bEdited = true;
	}
	else
		PrintCmd("Invalid step index");
}

void DeleteStepCmd(int index)
{
	if(index >= 0 && index < ACTION_FILE::MAXNUM_STEP)
	{
		for(int i=index; i<ACTION_FILE::MAXNUM_STEP; i++)
		{
			if(i == (ACTION_FILE::MAXNUM_STEP - 1))
			{
//				for(int jointIndex=0; jointIndex < MotionStatus::m_CurrentJoints.size(); jointIndex++)
				for(int jointIndex=0; jointIndex < NUM_OF_DXL; jointIndex++)
					Page.step[i].position[jointIndex + 1] = ACTION_FILE::INVALID_BIT_MASK;

				Page.step[i].pause = 0;
				Page.step[i].time = 0;
			}
			else
				Page.step[i] = Page.step[i + 1];

			DrawStep(i);
		}

		if(index < Page.header.stepnum)
		{
			if(Page.header.stepnum != 0)
			{
				DrawStepLine(true);
				Page.header.stepnum--;
				DrawStepLine(false);
			}

			GoToCursor(PAGEPARAM_COL, STEPNUM_ROW);
			printf( "%.3d", Page.header.stepnum );
		}

		bEdited = true;
	}
	else
		PrintCmd("Invalid step index");
}

void InsertStepCmd(int index)
{
	for(int jointIndex=0; jointIndex < NUM_OF_DXL; jointIndex++)
	{
		int id = jointIndex + 1;
		if(Step.position[id] & ACTION_FILE::TORQUE_OFF_BIT_MASK)
		{
			PrintCmd("Exist invalid joint value");
			return;
		}
	}

	if(index >= 0 && index < ACTION_FILE::MAXNUM_STEP)
	{
		for(int i=ACTION_FILE::MAXNUM_STEP-1; i>index; i-- )
		{
			Page.step[i] = Page.step[i-1];
			DrawStep(i);
		}

		Page.step[index] = Step;
		DrawStep(index);

		if(index == 0 || index < Page.header.stepnum)
		{
			if(Page.header.stepnum != ACTION_FILE::MAXNUM_STEP)
			{
				DrawStepLine(true);
				Page.header.stepnum++;
				DrawStepLine(false);
			}

			GoToCursor(PAGEPARAM_COL, STEPNUM_ROW);
			printf( "%.3d", Page.header.stepnum );
		}

		bEdited = true;
	}
	else
		PrintCmd("Invalid step index");
}

void MoveStepCmd(int src, int dst)
{
	if(src < 0 || src >= ACTION_FILE::MAXNUM_STEP)
	{
		PrintCmd("Invalid step index");
		return;
	}

	if(dst < 0 || dst >= ACTION_FILE::MAXNUM_STEP)
	{
		PrintCmd("Invalid step index");
		return;
	}

	if(src == dst)
		return;

	ACTION_FILE::STEP step = Page.step[src];
	if(src < dst)
	{
		for(int i=src; i<dst; i++)
		{
			Page.step[i] = Page.step[i + 1];
			DrawStep(i);
		}
	}
	else
	{
		for(int i=src; i>dst; i--)
		{
			Page.step[i] = Page.step[i - 1];
			DrawStep(i);
		}
	}

	Page.step[dst] = step;
	DrawStep(dst);
	bEdited = true;
}

void CopyCmd(int index)
{
	if(index == indexPage)
		return;

	if(ActionModule::GetInstance()->LoadPage(index, &Page) == true)
	{
		DrawPage();
		bEdited = true;
	}
	else
		PrintCmd("Invalid page index");
}

void NewCmd()
{
	ActionModule::GetInstance()->ResetPage(&Page);
	DrawPage();
	bEdited = true;
}

void GoCmd(int index)
{
	if(index < 0 || index >= ACTION_FILE::MAXNUM_STEP)
	{
		PrintCmd("Invalid step index");
		return;
	}

	if(index > Page.header.stepnum)
	{
		PrintCmd("Are you sure? (y/n)");
		if(_getch() != 'y')
		{
			ClearCmd();
			return;
		}
	}

	int id;

	UINT32_T wGoalPosition, wStartPosition, wDistance;
	int dwMaxDistance = 0;

	for(std::map<std::string, GroupSyncWrite *>::iterator _it = port_to_sync_write_go_cmd.begin(); _it != port_to_sync_write_go_cmd.end(); _it++)
	{
		_it->second->ClearParam();
	}


	for(int jointIndex=0; jointIndex < NUM_OF_DXL; jointIndex++)
	{
		id = jointIndex + 1;
		std::string _joint_name = _id_to_name[id];
		if(Page.step[index].position[id] & ACTION_FILE::INVALID_BIT_MASK)
		{
			PrintCmd("Exist invalid joint value");
			return;
		}

		if(_ctrl->ReadCtrlItem(_joint_name, "present_position",&wStartPosition, 0) != COMM_SUCCESS)
		{
			PrintCmd("Failed to read position");
			return;
		}

		wGoalPosition = Page.step[index].position[id];
		if( wStartPosition > wGoalPosition )
			wDistance = wStartPosition - wGoalPosition;
		else
			wDistance = wGoalPosition - wStartPosition;

//		wDistance = 200;
		wDistance = wDistance*0.05;
		if(wDistance > MAX_SPEED)
			wDistance = MAX_SPEED;

		if(dwMaxDistance < wDistance)
			dwMaxDistance = wDistance;

		int _offset = _op3_robot->dxls[_joint_name]->ConvertRadian2Value(_op3_robot->dxls[_joint_name]->dxl_state->position_offset) - _op3_robot->dxls[_joint_name]->value_of_0_radian_position;
		wGoalPosition = wGoalPosition + _offset;

		UINT8_T param[8];

		param[0] = DXL_LOBYTE(DXL_LOWORD(wDistance));
		param[1] = DXL_HIBYTE(DXL_LOWORD(wDistance));
		param[2] = DXL_LOBYTE(DXL_HIWORD(wDistance));
		param[3] = DXL_HIBYTE(DXL_HIWORD(wDistance));
		param[4] = DXL_LOBYTE(DXL_LOWORD(wGoalPosition));
		param[5] = DXL_HIBYTE(DXL_LOWORD(wGoalPosition));
		param[6] = DXL_LOBYTE(DXL_HIWORD(wGoalPosition));
		param[7] = DXL_HIBYTE(DXL_HIWORD(wGoalPosition));



		for(std::map<std::string, GroupSyncWrite *>::iterator _it = port_to_sync_write_go_cmd.begin(); _it != port_to_sync_write_go_cmd.end(); _it++)
		{
			_it->second->AddParam(id, param);
		}
	}

	for(std::map<std::string, GroupSyncWrite *>::iterator _it = port_to_sync_write_go_cmd.begin(); _it != port_to_sync_write_go_cmd.end(); _it++)
	{
		_it->second->TxPacket();
	}

	sleep(dwMaxDistance/1000 + 2);

	for(std::map<std::string, GroupSyncWrite *>::iterator _it = port_to_sync_write_go_cmd.begin(); _it != port_to_sync_write_go_cmd.end(); _it++)
	{
		_it->second->ClearParam();
	}

	for(int jointIndex=0; jointIndex < NUM_OF_DXL; jointIndex++)
	{
		id = jointIndex + 1;
		std::string _joint_name = _id_to_name[id];

		wDistance = 0;


		wGoalPosition = Page.step[index].position[id];

		int _offset = _op3_robot->dxls[_joint_name]->ConvertRadian2Value(_op3_robot->dxls[_joint_name]->dxl_state->position_offset) - _op3_robot->dxls[_joint_name]->value_of_0_radian_position;
		wGoalPosition = wGoalPosition + _offset;

		UINT8_T param[8];

		param[0] = DXL_LOBYTE(DXL_LOWORD(wDistance));
		param[1] = DXL_HIBYTE(DXL_LOWORD(wDistance));
		param[2] = DXL_LOBYTE(DXL_HIWORD(wDistance));
		param[3] = DXL_HIBYTE(DXL_HIWORD(wDistance));
		param[4] = DXL_LOBYTE(DXL_LOWORD(wGoalPosition));
		param[5] = DXL_HIBYTE(DXL_LOWORD(wGoalPosition));
		param[6] = DXL_LOBYTE(DXL_HIWORD(wGoalPosition));
		param[7] = DXL_HIBYTE(DXL_HIWORD(wGoalPosition));


		for(std::map<std::string, GroupSyncWrite *>::iterator _it = port_to_sync_write_go_cmd.begin(); _it != port_to_sync_write_go_cmd.end(); _it++)
		{
			_it->second->AddParam(id, param);
		}
	}

	for(std::map<std::string, GroupSyncWrite *>::iterator _it = port_to_sync_write_go_cmd.begin(); _it != port_to_sync_write_go_cmd.end(); _it++)
	{
		_it->second->TxPacket();
	}


	Step = Page.step[index];
	DrawStep(7);
	GoToCursor(CMD_COL, CMD_ROW);
	printf("Go Command Completed");
}

void SaveCmd()
{
	if(bEdited == false)
		return;

	if(ActionModule::GetInstance()->SavePage(indexPage, &Page) == true)
		bEdited = false;
}

void NameCmd()
{
	ClearCmd();
	GoToCursor(CMD_COL, CMD_ROW);
	printf("name: ");
	char name[80] = {0};
	fgets(name, 80, stdin);
	fflush(stdin);
	for(int i=0; i<=ACTION_FILE::MAXNUM_NAME; i++)
		Page.header.name[i] = name[i];
	DrawName();
	bEdited = true;
}



//void FoldRightHand()
//{
//	int error = 0;
//	int position_value = 0;
//	for( unsigned int index = 0; index< MotionStatus::m_CurrentJoints.size() ; index++)
//	{
//		int id = MotionStatus::m_CurrentJoints[index].m_ID;
//		if(id == RIGHT_HAND_ID) {
//			if(MotionStatus::m_CurrentJoints[index].m_DXL_Comm->GetDXLInstance()->ReadDWord(id, PRO54::P_PRESENT_POSITION_LL, &position_value, &error)
//					== COMM_RXSUCCESS)
//			{
//				position_value += HAND_POSITIO_DELTA;
//				if(position_value >= MAX_LIMIT_POS_FOR_HAND)
//					position_value = MAX_LIMIT_POS_FOR_HAND;
//				else if(position_value <= MIN_LIMIT_POS_FOR_HAND)
//					position_value = MIN_LIMIT_POS_FOR_HAND;
//
//
//				MotionStatus::m_CurrentJoints[index].m_DXL_Comm->GetDXLInstance()->WriteDWord(id, PRO54::P_GOAL_POSITION_LL, position_value, &error);
//				return;
//			}
//			else
//				return;
//		}
//		else
//			continue;
//
//	}
//
//	return;
//}

//void MirrorArmRight2LeftCmd(Thor::MotionManager *manager, int num_param, int *step_list)
//{
//	int row = 0, col = 0;
//	int *right_arm_pos = new int[DOF_OF_ARM];
//	int *left_arm_pos = new int[DOF_OF_ARM];
//
//	for(int step_list_idx = 0; step_list_idx < num_param; step_list_idx++) {
//		if((step_list[step_list_idx] < 0) || (step_list[step_list_idx] > 6)) {
//			PrintCmd("Invalid Parameter");
//			return;
//		}
//	}
//
//	bEdited = true;
//	bBeginCommandMode = false;
//	for(int step_list_idx = 0; step_list_idx < num_param; step_list_idx++) {
//
//		col = STP0_COL + 5*step_list[step_list_idx];
//
//		for(int arm_idx = 0; arm_idx < DOF_OF_ARM; arm_idx++) {
//			row = right_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			right_arm_pos[arm_idx] = GetValue();
//			if(right_arm_pos[arm_idx] == Action::INVALID_BIT_MASK || right_arm_pos[arm_idx] == Action::TORQUE_OFF_BIT_MASK)
//				continue;
//
//			left_arm_pos[arm_idx] = -1*(right_arm_pos[arm_idx] - 2048) + 2048;
//
//			row = left_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			SetValue(manager,left_arm_pos[arm_idx]);
//		}
//	}
//	bBeginCommandMode = true;
//	PrintCmd("Mirror : Right to Left");
//}
//
//void MirrorArmLeft2RightCmd(Thor::MotionManager *manager, int num_param, int *step_list)
//{
//	int row = 0, col = 0;
//	int *right_arm_pos = new int[DOF_OF_ARM];
//	int *left_arm_pos = new int[DOF_OF_ARM];
//
//	for(int step_list_idx = 0; step_list_idx < num_param; step_list_idx++) {
//		if((step_list[step_list_idx] < 0) || (step_list[step_list_idx] > 6)) {
//			PrintCmd("Invalid Parameter");
//			return;
//		}
//	}
//
//	bEdited = true;
//	bBeginCommandMode = false;
//	for(int step_list_idx = 0; step_list_idx < num_param; step_list_idx++) {
//
//		col = STP0_COL + 5*step_list[step_list_idx];
//
//		for(int arm_idx = 0; arm_idx < DOF_OF_ARM; arm_idx++) {
//			row = left_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			left_arm_pos[arm_idx] = GetValue();
//			if(left_arm_pos[arm_idx] == Action::INVALID_BIT_MASK || left_arm_pos[arm_idx] == Action::TORQUE_OFF_BIT_MASK)
//				continue;
//
//			right_arm_pos[arm_idx] = -1*(left_arm_pos[arm_idx] - 2048) + 2048;
//
//			row = right_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			SetValue(manager, right_arm_pos[arm_idx]);
//		}
//	}
//
//	bBeginCommandMode = true;
//	PrintCmd("Mirror : Left to Right");
//}
//
//
//void MirrorArmCmd(Thor::MotionManager *manager, int num_param, int *step_list)
//{
//	int row = 0, col = 0;
//	int *new_right_arm_pos = new int[DOF_OF_ARM];
//	int *new_left_arm_pos = new int[DOF_OF_ARM];
//	int *old_right_arm_pos = new int[DOF_OF_ARM];
//	int *old_left_arm_pos = new int[DOF_OF_ARM];
//
//	for(int step_list_idx = 0; step_list_idx < num_param; step_list_idx++) {
//		if((step_list[step_list_idx] < 0) || (step_list[step_list_idx] > 6)) {
//			PrintCmd("Invalid Parameter");
//			return;
//		}
//	}
//
//	bEdited = true;
//	bBeginCommandMode = false;
//	for(int step_list_idx = 0; step_list_idx < num_param; step_list_idx++) {
//
//		col = STP0_COL + 5*step_list[step_list_idx];
//
//		for(int arm_idx = 0; arm_idx < DOF_OF_ARM; arm_idx++) {
//			row = left_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			old_left_arm_pos[arm_idx] = GetValue();
//
//			row = right_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			old_right_arm_pos[arm_idx] = GetValue();
//
//			if((old_left_arm_pos[arm_idx] == Action::INVALID_BIT_MASK)
//					|| (old_left_arm_pos[arm_idx] == Action::TORQUE_OFF_BIT_MASK)
//					|| (old_right_arm_pos[arm_idx] == Action::INVALID_BIT_MASK)
//					|| (old_right_arm_pos[arm_idx] == Action::TORQUE_OFF_BIT_MASK))
//				continue;
//
//			new_right_arm_pos[arm_idx] = -1*(old_left_arm_pos[arm_idx]  - 2048) + 2048;
//			new_left_arm_pos[arm_idx]  = -1*(old_right_arm_pos[arm_idx] - 2048) + 2048;
//
//			row = right_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			SetValue(manager, new_right_arm_pos[arm_idx]);
//
//			row = left_arm_id_list[arm_idx] - 1;
//			GoToCursor(col, row);
//			SetValue(manager, new_left_arm_pos[arm_idx]);
//		}
//	}
//
//	bBeginCommandMode = true;
//	PrintCmd("Mirror : Mirror");
//}
//
//void StepCopyCmd(Thor::MotionManager *manager, int step_source, int step_destination)
//{
//	int row = 0, col = 0;
//	int *angle_value_list = new int[PAUSE_ROW];
//	int pause_time = 0;
//	int time = 0;
//
//	if((step_source < 0)
//			|| (step_source > 6)
//			|| (step_destination < 0)
//			|| (step_destination > 6)) {
//		PrintCmd("Invalid Parameter");
//		return;
//	}
//
//
//	bEdited = true;
//	bBeginCommandMode = false;
//
//	//copy
//	col = STP0_COL + 5*step_source;
//	for(int idx = 0;  idx < PAUSE_ROW; idx++) {
//		row = idx;
//		GoToCursor(col, row);
//		angle_value_list[idx] = GetValue();
//		if((angle_value_list[idx] == Action::INVALID_BIT_MASK)
//				|| (angle_value_list[idx] == Action::TORQUE_OFF_BIT_MASK))
//			continue;
//
//	}
//	row = PAUSE_ROW;
//	GoToCursor(col, row);
//	pause_time = GetValue();
//
//	row = SPEED_ROW;
//	GoToCursor(col, row);
//	time = GetValue();
//
//
//	//paste
//	col = STP0_COL + 5*step_destination;
//	for(int idx = 0;  idx < PAUSE_ROW; idx++) {
//		row = idx;
//		GoToCursor(col, row);
//		SetValue(manager, angle_value_list[idx]);
//	}
//
//	row = PAUSE_ROW;
//	GoToCursor(col, row);
//	SetValue(manager, pause_time);
//
//	row = SPEED_ROW;
//	GoToCursor(col, row);
//	SetValue(manager, time);
//
//	bBeginCommandMode = true;
//	PrintCmd("Step Copy");
//}
//
//static pid_t mp3_pid = -1;
//
//int PlaySound(const char* filename)
//{
//	//printf("--%s--\n", filename);
//
//    if(mp3_pid != -1)
//        kill(mp3_pid, SIGKILL);
//
//    mp3_pid = fork();
//
//    switch(mp3_pid)
//    {
//    case -1:
//        fprintf(stderr, "Fork failed!! \n");
//        break;
//    case 0:
//        //fprintf(stderr, "Playing MPEG stream from \"%s\" ...\n", filename);
//        //execl("/usr/bin/mplayer", "mplayer", filename, (char*)0);
//        execl("/usr/bin/mpg321", "mpg321", filename, "-q", (char*)0);
//		//execl("/usr/bin/mpg123", "mpg123", filename, "-q", (char*)0);
//        fprintf(stderr, "exec failed!! \n");
//        break;
//    default:
//        break;
//    }
//
//    return 1;
//}
//
//void PlayWithCmd(Thor::MotionManager* manager)
//{
//	ClearCmd();
//	GoToCursor(CMD_COL, CMD_ROW);
//	printf("PlayWith : ");
//	char name[128]= {0};
//	fgets(name, 128, stdin);
//	fflush(stdin);
//
//	//printf("  %s\n", name);
//
//	//last character set to 0
//	int i = 0;
//	for(i = 0; i <128; i++)
//	{
//		if(name[i] == 0)
//			break;
//	}
//
//	name[i-1] = 0;
//
//
//	if(PlaySound(name) != 1)
//		return;
//	else
//		PlayCmd(manager);
//}
