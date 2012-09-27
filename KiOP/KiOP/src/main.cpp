
//==========================================================================//
//============================ FICHIERS INCLUS =============================//

#include "Parametres.h"
#include "main.h"


//==========================================================================//
//=========================== VARIABLES GLOBALES ===========================//

// 0: Inactive; 1: Hand calibrating; 2: Main menu; 3: Normal tool mode; 4: Layout mode; 5: Mouse mode; 9: Return to main menu.
int g_currentState = 0;
int g_lastState = 0;
int g_moveCounter = 0;

// 0: Layout; 1: Move; 2: Contrast; 3: Zoom; 4: Scroll; 5: Mouse; 6: RedCross.
int g_currentTool = 3;
int g_lastTool = 3;
int g_toolToChoose = -1;
int g_totalTools = 6; // +1
int g_positionTool[7]; //position des outils dans le menu

// Layout
int g_totalLayoutTools = 6; //+1
int g_currentLayoutTool = 0;
int g_lastLayoutTool = 0;
bool g_layoutSelected = false;

float g_iconIdlePt = 192.0;
//float g_iconActivePt = 64.0; // ?
xn::Context g_context;
xn::DepthGenerator g_dpGen;
xn::DepthMetaData g_dpMD;
xn::HandsGenerator g_myHandsGenerator;
XnStatus g_status;

// Param�tre de profondeur de la main
float g_handDepthLimit;
float g_handDepthThreshold = 50.0;

bool g_handClosed = false;
bool g_handStateChanged = false;
bool g_handFlancMont = false;
bool g_handFlancDesc = false;
bool g_handClic = false;
bool g_lastHandClosed = false;

bool g_toolSelectable = false;
bool g_methodeMainFermeeSwitch = false;

// NITE
bool g_activeSession = false;
XnVSessionManager *gp_sessionManager;
XnVPointControl *gp_pointControl;
XnPoint3D g_handPt;
XnPoint3D g_lastPt;
XnVFlowRouter* g_pFlowRouter;
XnFloat g_fSmoothing = 0.0f;

// Qt
#ifdef _OS_WIN_
	int qargc = 0;
	char **qargv = NULL;
	QApplication app(qargc,qargv);
#endif
GraphicsView *gp_window;
GraphicsView *gp_windowActiveTool;
QGraphicsScene *gp_sceneActiveTool;
GraphicsView *gp_viewLayouts;
vector<Pixmap*> g_pix; //for main tools
vector<Pixmap*> g_pixL; //for layouts
Pixmap* gp_pixActive; //for activeTool
QColor g_toolColorActive = Qt::green;
QColor g_toolColorInactive = Qt::gray;

#ifdef _OS_WIN_
	TelnetClient g_telnet;
#endif

// KiOP //
CursorQt g_cursorQt;
HandClosedDetection g_hCD;
HandPoint g_hP;

string g_openNi_XML_FilePath;


//==========================================================================//
//============================== FONCTIONS =================================//

// Fonction d'initialisation
void Initialisation(void)
{
	ostringstream oss;
	const int nb(4);
	Point3D liste[nb];
	for (int i=0; i<nb; i++)
	{
		oss << i;
		liste[i].SetCoordinate(i*2,-i*3,-i*4);
		liste[i].Rename("liste[" + oss.str() + "]");
		oss.seekp(0);
	}

	cout << "= = = = = = = = = = = = = = = =" << endl;
	cout << "\tINIATISILATION" << endl;
	cout << "= = = = = = = = = = = = = = = =" << endl << endl;
	cout << "Resolution d'ecran : " << SCRSZW << "x" << SCRSZH << endl << endl;

	#ifdef _OS_WIN_
		ChangeCursor(0);
		InitGestionCurseurs();
	#endif
}

// Fonction de fermeture du programme
void CleanupExit()
{
	g_context.Shutdown();
	exit(1);
}


// Incr�mente une valeur sans jamais d�passer les bornes sup et inf
void IcrWithLimits(int &val, const int icr, const int limDown, const int limUp)
{
	val += icr;
	if (val > limUp) val = limUp;
	if (val < limDown) val = limDown;
}


inline bool isHandPointNull()
{
	return ((g_handPt.X == -1) ? true : false);
}


void chooseTool(int &currentTool, int &lastTool, int &totalTools)
{
	if (g_hP.DetectLeft())
	{
		if (g_moveCounter > 0)
			g_moveCounter = 0;
		g_moveCounter += g_hP.DeltaHandPt().X();
	}
	else if (g_hP.DetectRight())
	{
		if (g_moveCounter < 0)
			g_moveCounter = 0;
		g_moveCounter += g_hP.DeltaHandPt().X();
	}

	#if TEST_FLUIDITE
		//vitesse dans le menu en fonction de la distance
		int seuil = 20 - (abs(g_hP.Speed().X())+(g_hP.HandPt().Z()/300))/3;
	#else
		int seuil = 6;
	#endif

	//cout << "Seuil : " << seuil << endl;
	if (g_moveCounter <= -seuil)
	{
		// Go left in the menu
		lastTool = currentTool;
		IcrWithLimits(currentTool,-1,0,totalTools);
		g_moveCounter = 0;
	}
	else if (g_moveCounter >= seuil)
	{
		// Go right in the menu
		lastTool = currentTool;
		IcrWithLimits(currentTool,1,0,totalTools);
		g_moveCounter = 0;
	}
}



void browse(int currentTool, int lastTool, vector<Pixmap*> pix)
{
	//only set the pixmap geometry when needed
	if (lastTool != currentTool)
	{
		// On r�duit l'outil pr�c�dent
		pix.operator[](lastTool)->setGeometry(QRectF(lastTool*128.0, g_iconIdlePt, 64.0, 64.0));

		// On aggrandi l'outil courant
		pix.operator[](currentTool)->setGeometry(QRectF( (currentTool*128.0)-(currentTool==0?0:32), g_iconIdlePt-64, 128.0, 128.0));
	}
}


void CheckHandDown()
{
	//if (g_activeSession && g_toolSelectable)
	if (g_currentState >= 2)
	{
		if (g_hP.Speed().Y() > 24)
		{
			cout << "-- Main baissee, vitesse : " << g_hP.Speed().Y() << endl;
			gp_sessionManager->EndSession();
		}
	}
}

void CheckBaffe()
{
	//if (g_activeSession && g_toolSelectable)
	if (g_currentState >= 2)
	{
		int vitesseBaffe = abs(g_hP.Speed().X()) + abs(g_hP.HandPt().Z()/300);
		if (vitesseBaffe > 34)
		{
			cout << "-- Baffe detectee, vitesse : " << vitesseBaffe << endl;
			gp_sessionManager->EndSession();
		}
	}
}


void ChangeState(int newState)
{
	if (newState != g_lastState)
	{
		g_lastState = g_currentState;
		g_currentState = newState;

		cout << "- Entree dans l'etat no" << g_currentState << endl;
	}
}


bool SelectionDansUnMenu(short currentIcon)
{
	static short s_toolClic = 10;
	bool temp = false;

	if (g_handFlancMont)
	{
		// On enregistre l'outil sur lequel on a ferm� la main
		s_toolClic = currentIcon;
	}

	// Si l'outil change, on annule
	if (currentIcon != s_toolClic)
	{
		s_toolClic = 10;
		temp =  false;
	}
	else
	{
		// Sinon on entre dans l'outil selectionn� d�s que l'on ouvre la main
		if (g_handFlancDesc)
			temp =  true;
		else
			temp =  false;
	}

	return temp;
}



void handleState()
{
#ifdef _OS_MAC_
	static TelnetClient g_telnet;
#endif
	//static short s_toolClic = 10;

	CheckHandDown();
	CheckBaffe();

	switch (g_currentState)
	{
	// Session inactive
	case 0 :

		break; // case 0

	// Coucou effectu�, passage par sessionStart, calibrage de la main (200ms)
	case 1 :

		if (g_hP.Steady2())
		{
			ChangeState(2);
			Steady2Disable();

			g_toolSelectable = true;
			MenuOpaque();
		}
		break; // case 1

	// Apr�s le calibrage de la main, menu des outils
	case 2 :

		chooseTool(g_currentTool, g_lastTool, g_totalTools);
		browse(g_currentTool, g_lastTool, g_pix);

		if (SelectionDansUnMenu(g_currentTool))
		{
			g_telnet.connexion();
			switch(g_currentTool)
			{
			case 0:
				g_telnet.sendCommand(QString("\r\ndcmview2d:mouseLeftAction bonjour\r\n"));
				break;
			case 1:
				g_telnet.sendCommand(QString("\r\ndcmview2d:mouseLeftAction pan\r\n"));
				break;
			case 2:
				g_telnet.sendCommand(QString("\r\ndcmview2d:mouseLeftAction winLevel\r\n"));
				break;
			case 3:
				g_telnet.sendCommand(QString("\r\ndcmview2d:mouseLeftAction zoom\r\n"));
				break;
			case 4:
				g_telnet.sendCommand(QString("\r\ndcmview2d:mouseLeftAction sequence\r\n"));
				break;
			case 5:
				g_telnet.sendCommand(QString("\r\ndcmview2d:mouseLeftAction bonjour\r\n"));
				break;
			case 6:
				g_telnet.sendCommand(QString("\r\ndcmview2d:mouseLeftAction bonjour\r\n"));
				break;
			}

			// Si un des outils "normaux" a �t� selectionn�
			if ( (g_currentTool == 1) || (g_currentTool == 2) || (g_currentTool == 3) || (g_currentTool == 4) )
			{
				ChangeState(3);
				cout << "--- Selection de l'outil : " << g_currentTool << endl;

				gp_window->hide();
				gp_windowActiveTool->show();
				gp_windowActiveTool->setBackgroundBrush(QBrush(g_toolColorInactive, Qt::SolidPattern));
				gp_pixActive->load(QPixmap(":/images/"+g_pix.operator[](g_currentTool)->objectName()+".png").scaled(128,128));
			}
			
			// Si l'outil layout a �t� selectionn�
			else if (g_currentTool == 0)
			{
				ChangeState(4);
				g_currentLayoutTool = 0;
				g_lastLayoutTool = 6;
			}

			// Si l'outil souris a �t� selectionn�
			else if (g_currentTool == 5)
			{
				ChangeState(5);
				g_telnet.deconnexion();
				g_cursorQt.NewCursorSession();
				g_handDepthLimit = g_hP.HandPt().Z();

				Steady20Enable();

				gp_window->hide();
				gp_windowActiveTool->show();
			}

			// Si la croix a �t� selectionn�e
			else if (g_currentTool == g_totalTools)
			{
				cout << "-- Croix selectionnee" << endl;
				gp_sessionManager->EndSession();
			}
		}

		break; // case 2

	// Outil "normal" selectionn�
	case 3 :

		if (g_handClosed)
		{
			// On d�sactive les steadys
			Steady10Disable();
			Steady20Disable();

			gp_windowActiveTool->setBackgroundBrush(QBrush(g_toolColorActive, Qt::SolidPattern));

			switch (g_currentTool)
			{
			// Move
			case 1 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:move -- %1 %2\r\n")
				.arg(-SENSIBILITE_MOVE_X*g_hP.DeltaHandPt().X()).arg(-SENSIBILITE_MOVE_Y*g_hP.DeltaHandPt().Y()));
				break;

			// Contrast
			case 2 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:wl -- %1 %2\r\n")
				.arg(-SENSIBILITE_CONTRAST_X*g_hP.DeltaHandPt().X()).arg(-SENSIBILITE_CONTRAST_Y*g_hP.DeltaHandPt().Y()));
				break;

			// Zoom
			case 3 :
				if (g_hP.DetectBackward())
                {
					g_telnet.sendCommand(QString("\r\ndcmview2d:zoom -i %1\r\n").arg(SENSIBILITE_ZOOM));
                    cout << "--- Command sent : zoom+1" << endl;
                }
				if (g_hP.DetectForward())
                {
					g_telnet.sendCommand(QString("\r\ndcmview2d:zoom -d %1\r\n").arg(SENSIBILITE_ZOOM));
                    cout << "--- Command sent : zoom-1" << endl;
                }
				break;

			// Scroll
			case 4 :
				if (g_hP.DetectBackward())
					g_telnet.sendCommand(QString("\r\ndcmview2d:scroll -i %1\r\n").arg(SENSIBILITE_SCROLL));
				if (g_hP.DetectForward())
					g_telnet.sendCommand(QString("\r\ndcmview2d:scroll -d %1\r\n").arg(SENSIBILITE_SCROLL));
				break;

			} // end switch (g_currentTool)
		}

		// Si la main est ouverte
		else
		{
			gp_windowActiveTool->setBackgroundBrush(QBrush(g_toolColorInactive, Qt::SolidPattern));

			Steady10Enable();
			Steady20Enable();
			if (g_hP.Steady10())
				ChangeState(9); // Pr�paration pour le retour au menu
		}

		break; // case 3

	// Outil layout selectionn�
	case 4 :

		gp_window->hide();
		gp_windowActiveTool->hide();
		gp_viewLayouts->show();

		chooseTool(g_currentLayoutTool, g_lastLayoutTool, g_totalLayoutTools);
		browse(g_currentLayoutTool,g_lastLayoutTool, g_pixL);

		if (SelectionDansUnMenu(g_currentLayoutTool))
		{
			switch(g_currentLayoutTool)
			{
			case 0 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:layout -i 1x1\r\n"));
				break;
			case 1 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:layout -i 1x2\r\n"));
				break;
			case 2 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:layout -i 2x1\r\n"));
				break;
			case 3 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:layout -i layout_c1x2\r\n"));
				break;
			case 4 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:layout -i layout_c2x1\r\n"));
				break;
			case 5 :
				g_telnet.sendCommand(QString("\r\ndcmview2d:layout -i 2x2\r\n"));
				break;
			case 6 :
				ChangeState(9);
				break;
			}
		}

		break; // case 4

	// Outil souris selectionn�
	case 5 :

		if (g_cursorQt.InCursorSession())
		{
			// Distance limite de la main au capteur
			if (g_hP.HandPt().Z() < (g_handDepthLimit + g_handDepthThreshold))
			{
				g_cursorQt.SetMoveEnable();
				g_cursorQt.SetClicEnable();
				gp_windowActiveTool->setBackgroundBrush(QBrush(g_toolColorActive, Qt::SolidPattern));
				if (g_handFlancMont)
					gp_pixActive->load(QPixmap(":/images/mouse_fermee.png").scaled(128,128));
				else if (g_handFlancDesc)
					gp_pixActive->load(QPixmap(":/images/mouse.png").scaled(128,128));
			}
			else
			{
				g_cursorQt.SetMoveDisable();
				g_cursorQt.SetClicDisable();
				gp_windowActiveTool->setBackgroundBrush(QBrush(g_toolColorInactive, Qt::SolidPattern));
			}

			// Appel de la m�thode pour d�placer le curseur
			g_cursorQt.MoveCursor(g_hP.HandPt());
		}

		// Sortie du mode souris
		else
		{
			ChangeState(9);
		}

		break; // case 5

	// Pr�paration pour le retour au menu
	case 9 :

		ChangeState(2);
		g_telnet.deconnexion();

		g_lastTool = g_currentTool;
		g_currentTool = g_totalTools;

		browse(g_currentTool,g_lastTool,g_pix);

		gp_window->show();
		gp_viewLayouts->hide();
		gp_windowActiveTool->hide();

		Steady10Disable();
		Steady20Disable();

		break; // case 9

	} // end switch (g_currentState)
}


void glutKeyboard (unsigned char key, int x, int y)
{
	static int test = 0;
	float tmp = 0.0;
	switch (key)
	{

	// Exit
	case 27 :
		#ifdef _OS_WIN_
			ChangeCursor(0);
		#endif
		CleanupExit();
		break;

	case 'i' :
		g_hP.IncrementSmooth(1,1,1);
		//g_hP.Smooth().Print();
		//g_hP.HandPt().Print();
		IcrWithLimits(test,3,0,10);
		cout << "-- test : " << test << endl;
		break;

	case 'o' :
		g_hP.IncrementSmooth(-1,-1,-1);
		//g_hP.Smooth().Print();
		IcrWithLimits(test,-3,0,10);
		cout << "-- test : " << test << endl;
		break;

	case 's' :
		// Toggle smoothing
		if (g_fSmoothing == 0)
			g_fSmoothing = 0.1;
		else 
			g_fSmoothing = 0;
		g_myHandsGenerator.SetSmoothing(g_fSmoothing);
		break;

	case 'a' :
		//show some data for debugging purposes
		cout << "x= " << g_hP.HandPt().X() << " ; y= " << g_hP.HandPt().Y() << " ; z= " << g_hP.HandPt().Z() << " ;   " << g_fSmoothing << " ;   " << g_currentState << endl;
		break;

	case 'y' :
		//show tools position
		for (int i=0; i<=g_totalTools; i++)
		{
			cout << "tool" << i << " : " << g_positionTool[i] << endl;
		}
		break;

	case 'e' :
		// end current session
		gp_sessionManager->EndSession();
		break;

	//case 't' :
	//	g_methodeMainFermeeSwitch = !g_methodeMainFermeeSwitch;
	//	cout << "-- Switch Methode main fermee (" << (g_methodeMainFermeeSwitch?2:1) << ")" << endl;
	//	break;

	case '1' :
		RepositionnementFenetre(1);
		break;
	case '2' :
		RepositionnementFenetre(2);
		break;
	case '3' :
		RepositionnementFenetre(3);
		break;
	case '4' :
		RepositionnementFenetre(4);
		break;
	}
}



void glutDisplay()
{
	static unsigned compteurFrame = 0; compteurFrame++;

	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);	//clear the gl buffers
	g_status = g_context.WaitAnyUpdateAll();	//first update the g_context - refresh the depth/image data coming from the sensor
	
	// if the update failed, i.e. couldn't be read
	if(g_status != XN_STATUS_OK)
	{
		cout << "\nERROR:Read failed... Quitting!\n" << endl;	//print error message
		exit(0);	//exit the program
	}
	else
	{
		if(g_activeSession)
			gp_sessionManager->Update(&g_context);
		g_dpGen.GetMetaData(g_dpMD);
		long xSize = g_dpMD.XRes();
		long ySize = g_dpMD.YRes();
		long totalSize = xSize * ySize;

		const XnDepthPixel*	depthMapData;
		depthMapData = g_dpMD.Data();

		int i, j, colorToSet;
		int depth;

		glLoadIdentity();
		glOrtho(0, xSize, ySize, 0, -1, 1);

		glBegin(GL_POINTS);
		for(i=0;i<xSize;i+=RES_WINDOW_GLUT)	// width
		{
			for(j=0;j<ySize;j+=RES_WINDOW_GLUT)	// height
			{
				depth = g_dpMD(i,j);
				colorToSet = MAX_COLOR - (depth/COLORS);

				if((depth < DP_FAR) && (depth > DP_CLOSE))
				{
					if (g_activeSession)
					{
						if (g_hP.HandPt().Z() < DISTANCE_MAX_DETECTION)
						{
							glColor3ub(0,colorToSet,0);
						}
						else
						{
							glColor3ub(colorToSet,0,0);
						}
					}
					else
						glColor3ub(colorToSet,colorToSet,colorToSet);
					glVertex2i(i,j);
				}
			}
		}
		glEnd();	// End drawing sequence



		if (g_hP.Steady2())
		{
			//cout << "--- Steady 2" << endl;
			g_toolSelectable = true;
		}
		if (g_hP.Steady10())
		{
			//cout << "--- Steady 10" << endl;

			// Si mode souris
			if (g_currentState == 5)
			{
				g_cursorQt.SteadyDetected(10);
			}
		}
		if (g_hP.Steady20())
		{
			//cout << "--- Steady 20" << endl;

			// Si mode souris
			if (g_currentState == 5)
			{
				g_cursorQt.SteadyDetected(20);
			}
		}
		if (g_hP.NotSteady())
		{
			//cout << "--- Not Steady" << endl;

			// Mode souris
			if (g_currentState == 5)
			{
				//cursor.NotSteadyDetected();
			}
		}

		// Mise � jour de la detection de la main fermee
		if	( g_activeSession && (isHandPointNull() == false))
			UpdateHandClosed();

		if	( g_activeSession && (isHandPointNull() == false))
		{

			//cout << "Vitesse : " << g_hP.Speed() << endl;
			//cout << "handpt : " << g_hP.HandPt().Z() << endl;


			int size = 5;						// Size of the box
			glColor3f(255,255,255);	// Set the color to white
			glBegin(GL_QUADS);
				glVertex2i(g_hP.HandPtBrut().X()-size,g_hP.HandPtBrut().Y()-size);
				glVertex2i(g_hP.HandPtBrut().X()+size,g_hP.HandPtBrut().Y()-size);
				glVertex2i(g_hP.HandPtBrut().X()+size,g_hP.HandPtBrut().Y()+size);
				glVertex2i(g_hP.HandPtBrut().X()-size,g_hP.HandPtBrut().Y()+size);
			glEnd();

			size = 4;
			glColor3f(0,0,255);
			glBegin(GL_QUADS);
				glVertex2i(g_hP.HandPtBrutFiltre().X()-size,g_hP.HandPtBrutFiltre().Y()-size);
				glVertex2i(g_hP.HandPtBrutFiltre().X()+size,g_hP.HandPtBrutFiltre().Y()-size);
				glVertex2i(g_hP.HandPtBrutFiltre().X()+size,g_hP.HandPtBrutFiltre().Y()+size);
				glVertex2i(g_hP.HandPtBrutFiltre().X()-size,g_hP.HandPtBrutFiltre().Y()+size);
			glEnd();

			size = 5;
			glColor3f(255,0,0);
			glBegin(GL_QUADS);
				glVertex2i(g_hP.HandPt().X()-size,g_hP.HandPt().Y()-size);
				glVertex2i(g_hP.HandPt().X()+size,g_hP.HandPt().Y()-size);
				glVertex2i(g_hP.HandPt().X()+size,g_hP.HandPt().Y()+size);
				glVertex2i(g_hP.HandPt().X()-size,g_hP.HandPt().Y()+size);
			glEnd();
		}

		//========== HAND POINT ==========//
		if	( g_activeSession && (isHandPointNull() == false)
				&& (g_hCD.ROI_Pt().x() >= 0)
				&& (g_hCD.ROI_Pt().y() >= 0)
				&& (g_hCD.ROI_Pt().x() <= (RES_X - g_hCD.ROI_Size().width()))
				&& (g_hCD.ROI_Pt().y() <= (RES_Y - g_hCD.ROI_Size().height())) )
		{
			// Cadre de la main
			glColor3f(255,255,255);
			glBegin(GL_LINE_LOOP);
				glVertex2i(g_hCD.ROI_Pt().x(), g_hCD.ROI_Pt().y());
				glVertex2i(g_hCD.ROI_Pt().x()+g_hCD.ROI_Size().width(), g_hCD.ROI_Pt().y());
				glVertex2i(g_hCD.ROI_Pt().x()+g_hCD.ROI_Size().width(), g_hCD.ROI_Pt().y()+g_hCD.ROI_Size().height());
				glVertex2i(g_hCD.ROI_Pt().x(), g_hCD.ROI_Pt().y()+g_hCD.ROI_Size().height());
			glEnd();

			// mode Souris
			if ( (g_currentState == 5) && (g_cursorQt.InCursorSession()) )
			{
				// Souris SteadyClic
				if			(g_cursorQt.CursorType() == 1)
				{
					//if (cursor.CheckExitMouseMode())
					//if (g_cursorQt.ExitMouseMode())
						//cursor.ChangeState(0);
				}

				//Souris HandClosedClic
				else if ((g_cursorQt.CursorType() == 2) && (g_cursorQt.CursorInitialised()))
				{
					if (g_handFlancMont)
						g_cursorQt.SetHandClosed(true);
					else if (g_handFlancDesc)
						g_cursorQt.SetHandClosed(false);
				}
			}

			// Affichage des carr�s de couleurs pour indiquer l'etat de la main
			if (g_handClosed)
				glColor3ub(255,0,0);
			else 
				glColor3ub(0,0,255);
			int cote = 50;
			int carreX = xSize-(cote+10), carreY = 10;
			glRecti(carreX,carreY,carreX+cote,carreY+cote);
		}
	}
	glutSwapBuffers();

	// Gestion des �tats
	handleState();
}


void UpdateHandClosed(void)
{
	if (g_toolSelectable)
	{
		g_hCD.Update((g_methodeMainFermeeSwitch?2:1),g_dpMD,g_hP.HandPtBrut());
		g_handStateChanged = g_hCD.HandClosedStateChanged();
		g_handFlancMont = g_hCD.HandClosedFlancMont();
		g_handFlancDesc = g_hCD.HandClosedFlancDesc();
		g_lastHandClosed = g_handClosed;
		g_handClosed = g_hCD.HandClosed();
		g_handClic = g_hCD.HandClosedClic(19); //9

		//// Controle de detection de la main fermee //
		//if (g_handStateChanged)
		//	cout << endl << "---- Chgmt d'etat de la main!" << endl;
		//if (g_handFlancMont)
		//	cout << endl << "---- Fermeture de la main!" << endl;
		//if (g_handFlancDesc)
		//	cout << endl << "---- Ouverture de la main!" << endl;
		//if (g_handClosed)
		//	cout << endl << "---- Main Fermee!" << endl;
		//else
		//	cout << endl << "---- Main Ouverte!" << endl;
		//if (g_handClic)
		//	cout << endl << "---- Clic de la main!" << endl;
	}
}


void initGL(int argc, char *argv[])
{
	glutInit(&argc,argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(INIT_WIDTH_WINDOW, INIT_HEIGHT_WINDOW);

	// Fen�tre de donn�es source
	glutCreateWindow(TITLE);
	RepositionnementFenetre(INIT_POS_WINDOW);
	glutKeyboardFunc(glutKeyboard);
	glutDisplayFunc(glutDisplay);

	// Idle callback (pour toutes les fen�tres)
	glutIdleFunc(glutDisplay);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
}


//==========================================================================//
//================================= MAIN ===================================//

int main(int argc, char *argv[])
{
	Initialisation();

	g_openNi_XML_FilePath = argv[0];
#if defined _OS_WIN_
	int p = g_openNi_XML_FilePath.find(".exe");
	g_openNi_XML_FilePath = g_openNi_XML_FilePath.substr(0,p-4).append("openni.xml");
#elif defined _OS_MAC_
	int p = g_openNi_XML_FilePath.find(".app");
	g_openNi_XML_FilePath = g_openNi_XML_FilePath.substr(0,p+4).append("/Contents/Resources/openni.xml");
#endif

	cout << "Chemin de openni.xml : " << g_openNi_XML_FilePath << endl << endl;

	//------ OPEN_NI / NITE / OPENGL ------//
	xn::EnumerationErrors errors;

	g_status = g_context.InitFromXmlFile(g_openNi_XML_FilePath.c_str());
	CHECK_ERRORS(g_status, errors, "InitFromXmlFile");
	CHECK_STATUS(g_status, "InitFromXml");

	//si le g_context a �t� initialis� correctement
	g_status = g_context.FindExistingNode(XN_NODE_TYPE_DEPTH, g_dpGen);
	CHECK_STATUS(g_status, "Find depth generator");
	g_status = g_context.FindExistingNode(XN_NODE_TYPE_HANDS, g_myHandsGenerator);
	CHECK_STATUS(g_status, "Find hands generator");

	// NITE 
	gp_sessionManager = new XnVSessionManager();

	//Focus avec un coucou et Refocus avec "RaiseHand" 
	g_status = gp_sessionManager->Initialize(&g_context,"Wave","Wave,RaiseHand");
	CHECK_STATUS(g_status,"Session manager");

	gp_sessionManager->RegisterSession(&g_context,sessionStart,sessionEnd,FocusProgress);
	gp_sessionManager->SetQuickRefocusTimeout(3000);

	g_pFlowRouter = new XnVFlowRouter;

	gp_pointControl = new XnVPointControl("Point Tracker");
	gp_pointControl->RegisterPrimaryPointCreate(&g_context,pointCreate);
	gp_pointControl->RegisterPrimaryPointDestroy(&g_context,pointDestroy);
	gp_pointControl->RegisterPrimaryPointUpdate(&g_context,pointUpdate);

	// Wave detector
	XnVWaveDetector waveDetect;
	waveDetect.RegisterWave(&g_context,&Wave_Detected);
	//waveDetect.SetFlipCount(10);
	//waveDetect.SetMaxDeviation(1);
	//waveDetect.SetMinLength(100);

	// Add Listener
	gp_sessionManager->AddListener(gp_pointControl);
	gp_sessionManager->AddListener(g_pFlowRouter);
	gp_sessionManager->AddListener(&waveDetect);
		
	nullifyHandPoint();
	g_myHandsGenerator.SetSmoothing(g_fSmoothing);

	// Initialization done. Start generating
	g_status = g_context.StartGeneratingAll();
	CHECK_STATUS(g_status, "StartGenerating");

	initGL(argc,argv);


	// Qt
#ifdef _OS_MAC_
	int qargc = 0;
	char **qargv = NULL;
	QApplication app(qargc,qargv);
#endif

	g_cursorQt = CursorQt(2);
	gp_window = new GraphicsView(NULL);
	gp_windowActiveTool = new GraphicsView(NULL);
	gp_sceneActiveTool = new QGraphicsScene(0,0,128,128);
	gp_viewLayouts = new GraphicsView(NULL);
	gp_pixActive = new Pixmap(QPixmap()); //for activeTool



	//================== QT ===================//

	// Initialisation des ressources et cr�ation de la fen�tre avec les ic�nes
	Q_INIT_RESOURCE(images);

//#if defined _OS_WIN_
	Pixmap *p1 = new Pixmap(QPixmap(":/images/layout.png").scaled(64,64));
	Pixmap *p2 = new Pixmap(QPixmap(":/images/move.png").scaled(64,64));
	Pixmap *p3 = new Pixmap(QPixmap(":/images/contrast.png").scaled(64,64));
	Pixmap *p4 = new Pixmap(QPixmap(":/images/zoom.png").scaled(64,64));
	Pixmap *p5 = new Pixmap(QPixmap(":/images/scroll.png").scaled(64,64));
	Pixmap *p6 = new Pixmap(QPixmap(":/images/mouse.png").scaled(64,64));
	Pixmap *p7 = new Pixmap(QPixmap(":/images/stop.png").scaled(64,64));
//#elif defined _OS_MAC_
//	Pixmap *p1 = new Pixmap(QPixmap(":/images/layout.png").scaled(64,64));
//	Pixmap *p2 = new Pixmap(QPixmap(":/images/move.png").scaled(64,64));
//	Pixmap *p3 = new Pixmap(QPixmap(":/images/contrast.png").scaled(64,64));
//	Pixmap *p4 = new Pixmap(QPixmap(":/images/zoom.png").scaled(64,64));
//	Pixmap *p5 = new Pixmap(QPixmap(":/images/scroll.png").scaled(64,64));
//	Pixmap *p6 = new Pixmap(QPixmap(":/images/mouse.png").scaled(64,64));
//	Pixmap *p7 = new Pixmap(QPixmap(":/images/stop.png").scaled(64,64));
//#endif

	p1->setObjectName("layout");
	p2->setObjectName("move");
	p3->setObjectName("contrast");
	p4->setObjectName("zoom");
	p5->setObjectName("scroll");
	p6->setObjectName("mouse");
	p7->setObjectName("stop");

	p1->setGeometry(QRectF(  0.0, g_iconIdlePt, 64.0, 64.0));
	p2->setGeometry(QRectF(128.0, g_iconIdlePt, 64.0, 64.0));
	p3->setGeometry(QRectF(256.0, g_iconIdlePt, 64.0, 64.0));
	p4->setGeometry(QRectF(384.0, g_iconIdlePt, 64.0, 64.0));
	p5->setGeometry(QRectF(512.0, g_iconIdlePt, 64.0, 64.0));
	p6->setGeometry(QRectF(640.0, g_iconIdlePt, 64.0, 64.0));
	p7->setGeometry(QRectF(768.0, g_iconIdlePt, 64.0, 64.0));

	g_pix.push_back(p1);
	g_pix.push_back(p2);
	g_pix.push_back(p3);
	g_pix.push_back(p4);
	g_pix.push_back(p5);
	g_pix.push_back(p6);
	g_pix.push_back(p7);

	//gp_window->setSize(1024,288);
	gp_window->setSize(896,256);
	
	//gp_window->setSize(548,gp_window->getResY()-100);
	QGraphicsScene *scene = new QGraphicsScene(0,0,896,256);
	//QGraphicsScene *scene = new QGraphicsScene(0,(-gp_window->getResY())+488,548,gp_window->getResY()-100);
	scene->addItem(p1);
	scene->addItem(p2);
	scene->addItem(p3);
	scene->addItem(p4);
	scene->addItem(p5);
	scene->addItem(p6);
	scene->addItem(p7);
	gp_window->setScene(scene);

	gp_sceneActiveTool->addItem(gp_pixActive);
	//gp_windowActiveTool->setSize(126,126);
	gp_windowActiveTool->setScene(gp_sceneActiveTool);
	gp_windowActiveTool->setGeometry(gp_window->getResX()-128,gp_window->getResY()-168,128,128);


	////////////// LAYOUT
//#if defined _OS_WIN_
	Pixmap *l1 = new Pixmap(QPixmap(":/images/layouts/_1x1.png").scaled(64,64));
	Pixmap *l2 = new Pixmap(QPixmap(":/images/layouts/_1x2.png").scaled(64,64));
	Pixmap *l3 = new Pixmap(QPixmap(":/images/layouts/_2x1.png").scaled(64,64));
	Pixmap *l4 = new Pixmap(QPixmap(":/images/layouts/_3a.png").scaled(64,64));
	Pixmap *l5 = new Pixmap(QPixmap(":/images/layouts/_3b.png").scaled(64,64));
	Pixmap *l6 = new Pixmap(QPixmap(":/images/layouts/_2x2.png").scaled(64,64));
	Pixmap *l7 = new Pixmap(QPixmap(":/images/stop.png").scaled(64,64));
//#elif defined _OS_MAC_
//	Pixmap *l1 = new Pixmap(QPixmap(":/images/layouts/_1x1.png").scaled(64,64));
//	Pixmap *l2 = new Pixmap(QPixmap(":/images/layouts/_1x2.png").scaled(64,64));
//	Pixmap *l3 = new Pixmap(QPixmap(":/images/layouts/_2x1.png").scaled(64,64));
//	Pixmap *l4 = new Pixmap(QPixmap(":/images/layouts/_3a.png").scaled(64,64));
//	Pixmap *l5 = new Pixmap(QPixmap(":/images/layouts/_3b.png").scaled(64,64));
//	Pixmap *l6 = new Pixmap(QPixmap(":/images/layouts/_2x2.png").scaled(64,64));
//	Pixmap *l7 = new Pixmap(QPixmap(":/images/stop.png").scaled(64,64));
//#endif

	l1->setObjectName("1x1");
	l2->setObjectName("1x2");
	l3->setObjectName("2x1");
	l4->setObjectName("3a");
	l5->setObjectName("3b");
	l6->setObjectName("2x2");
	l7->setObjectName("stop");
	
	l1->setGeometry(QRectF(  0.0, g_iconIdlePt, 64.0, 64.0));
	l2->setGeometry(QRectF(128.0, g_iconIdlePt, 64.0, 64.0));
	l3->setGeometry(QRectF(256.0, g_iconIdlePt, 64.0, 64.0));
	l4->setGeometry(QRectF(384.0, g_iconIdlePt, 64.0, 64.0));
	l5->setGeometry(QRectF(512.0, g_iconIdlePt, 64.0, 64.0));
	l6->setGeometry(QRectF(640.0, g_iconIdlePt, 64.0, 64.0));
	l7->setGeometry(QRectF(768.0, g_iconIdlePt, 64.0, 64.0));

	g_pixL.push_back(l1);
	g_pixL.push_back(l2);
	g_pixL.push_back(l3);
	g_pixL.push_back(l4);
	g_pixL.push_back(l5);
	g_pixL.push_back(l6);
	g_pixL.push_back(l7);

	gp_viewLayouts->setSize(896,256);
	QGraphicsScene *sceneLayout = new QGraphicsScene(0,0,896,256);
	sceneLayout->addItem(l1);
	sceneLayout->addItem(l2);
	sceneLayout->addItem(l3);
	sceneLayout->addItem(l4);
	sceneLayout->addItem(l5);
	sceneLayout->addItem(l6);
	sceneLayout->addItem(l7);
	gp_viewLayouts->setScene(sceneLayout);

	//gp_viewLayouts->show();

	
	/*for(int i=0; i<=g_totalTools; i++){
		QString chemin = ":/images/Resources/_"+g_pix.operator[](i)->objectName()+".png";
		//printf("\n"+chemin.toAscii()+"\n");
		int posi = g_positionTool[i];
		g_pix.operator[](i)->setGeometry(QRectF( posi*60.0, posi*(-10.0), 128.0, 128.0));
		g_pix.operator[](i)->load(QPixmap(chemin).scaled(78+(posi*(10)),78+(posi*(10))));
	}*/

	// Boucle principale
	glutMainLoop();
	
	return app.exec();
}



/**** CALLBACK DEFINITIONS ****/

/**********************************************************************************
Session started event handler. Session manager calls this when the session begins
**********************************************************************************/
void XN_CALLBACK_TYPE sessionStart(const XnPoint3D& ptPosition, void* UserCxt)
{
	ChangeState(1);

	g_activeSession = true;
	g_toolSelectable = false;

	g_currentTool = g_totalTools;
	g_lastTool = 0;

	gp_window->show();
	MenuTransparent();
	gp_viewLayouts->hide();
	gp_windowActiveTool->hide();

	Steady2Enable();
	Steady10Disable();
	Steady20Disable();

	g_hCD.ResetCompteurFrame();

	static int compteurSession = 1;
	cout << endl << "Debut de la session : " << compteurSession++ 
		<< "e" << (compteurSession==1?"re":"") << " fois" << endl << endl;
}

/**********************************************************************************
session end event handler. Session manager calls this when session ends
**********************************************************************************/
void XN_CALLBACK_TYPE sessionEnd(void* UserCxt)
{
	ChangeState(0);

	//g_telnet.deconnexion();

	g_activeSession = false;
	g_toolSelectable = false;

	g_currentTool = g_totalTools;
	g_lastTool = 0;

	// On r�duit tous les outils et layouts
	for (int i=0; i<=g_totalTools; i++)
		g_pix.operator[](i)->setGeometry(QRectF(i*128.0, g_iconIdlePt, 64.0, 64.0));
	for (int i=0; i<=g_totalLayoutTools; i++)
		g_pixL.operator[](i)->setGeometry(QRectF(i*128.0, g_iconIdlePt, 64.0, 64.0));

	gp_window->hide();
	gp_viewLayouts->hide();
	gp_windowActiveTool->hide();

	Steady2Disable();
	Steady10Disable();
	Steady20Disable();

	XnPoint3D ptTemp;
	ptTemp.X = g_hP.HandPt().X();
	ptTemp.Y = g_hP.HandPt().Y();
	ptTemp.Z = g_hP.HandPt().Z();
	g_lastPt = ptTemp;
	
	cout << endl << "Fin de la session" << endl << endl;
}


/**********************************************************************************
point created event handler. this is called when the gp_pointControl detects the creation
of the hand point. This is called only once when the hand point is detected
**********************************************************************************/
void XN_CALLBACK_TYPE pointCreate(const XnVHandPointContext *pContext, const XnPoint3D &ptFocus, void *cxt)
{
	XnPoint3D coords(pContext->ptPosition);
	g_dpGen.ConvertRealWorldToProjective(1,&coords,&g_handPt);
	g_lastPt = g_handPt;

	g_hP.Update(g_handPt);
}
/**********************************************************************************
Following the point created method, any update in the hand point coordinates are 
reflected through this event handler
**********************************************************************************/
void XN_CALLBACK_TYPE pointUpdate(const XnVHandPointContext *pContext, void *cxt)
{
	XnPoint3D coords(pContext->ptPosition);
	g_dpGen.ConvertRealWorldToProjective(1,&coords,&g_handPt);

	g_hP.Update(g_handPt);
}
/**********************************************************************************
when the point can no longer be tracked, this event handler is invoked. Here we 
nullify the hand point variable 
**********************************************************************************/
void XN_CALLBACK_TYPE pointDestroy(XnUInt32 nID, void *cxt)
{
	cout << "Point detruit" << endl;

	nullifyHandPoint();
}


// Callback for no hand detected
void XN_CALLBACK_TYPE NoHands(void* UserCxt)
{
	g_cursorQt.EndCursorSession();
}

// Callback for when the focus is in progress
void XN_CALLBACK_TYPE FocusProgress(const XnChar* strFocus, 
		const XnPoint3D& ptPosition, XnFloat fProgress, void* UserCxt)
{
	//cout << "Focus progress: " << strFocus << " @(" << ptPosition.X << "," 
	//			<< ptPosition.Y << "," << ptPosition.Z << "): " << fProgress << "\n" << endl;

	/// Pour r�afficher l'�cran s'il s'est �teint
	SimulateCtrlBar();
}


// Callback for wave
void XN_CALLBACK_TYPE Wave_Detected(void *pUserCxt)
{
	cout << "-- Wave detected" << endl;
}


void SimulateCtrlBar(void)
{
#if defined _OS_WIN_
	// Simulate a key press
	keybd_event(VK_LCONTROL,0x45,KEYEVENTF_EXTENDEDKEY | 0,0);
	// Simulate a key release
	keybd_event(VK_LCONTROL,0x45,KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP,0);
#endif
}


void MenuTransparent(void)
{
	gp_window->setWindowOpacity(qreal(0.4));
}

void MenuOpaque(void)
{
	gp_window->setWindowOpacity(qreal(1.0));
}

