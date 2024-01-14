#pragma once

#include <QObject>
#include <QLoggingCategory>
#include "BCN_Types.h"
#include "BC_Types.h"
#include "TC_Types.h"
#include "myTypes.h"
#include "tmk_BC_common.h"
#include <QSemaphore>

Q_DECLARE_LOGGING_CATEGORY(tmk_category)

class TMK_Worker : public QObject
{
    Q_OBJECT
    //TMtable ask every time and usually cause busy error, pseudo_CAS500 shouldn't inform user about busy in this case
    QSemaphore *TMtable_pending; //should contains zero or one request for TMtable

    quint32 good_starts=0; //how many good communications
    quint32 bad_starts=0; //how many fail communications

    void CheckTmkEvent(const int fWaitTime);
    int tmk_BC_init(void);
    void tmk_RT_BC_FULL(void);
    void tmk_RT_transmit_BC(const quint16 subaddr, const quint8 len);

public:
    bool isCommunicable= false; //if tmk_BC_init()==0 then true else false

    explicit TMK_Worker(QObject *parent = nullptr);
    ~TMK_Worker();

public slots:
    void tmk_RT_transmit_BC_TeleMetryTable();
    void tmk_CMBPUMPPWR(const pump_num_t num, const on_off_state_t state);
    void tmk_CMBCAMPWR(const quint8 num, const on_off_state_t state);
    void tmk_CMBSTPPWR(const bc_num_t num, const on_off_state_t state); //stepper
    void tmk_CMBHTRPWR(const htr_num_t num, const on_off_state_t state); //Heater 1~8
    void tmk_CMBSBPWR(const sb_num_t num, const on_off_state_t state); //sensor board
    void tmk_CMBTCSPWR(const tc_set_num_t num, const on_off_state_t state); //THERMAL COUPLE set
    void tmk_CMBUVPWR(const uv_num_t num, const on_off_state_t state); //uv LED
    void tmk_CMBOFF();
    void tmk_CMBOFFCNF();
    void tmk_CMBCAPTIMG(const cam_num_t num, const quint8 shorts_amo); //camera num captures some photos
    void tmk_CMBCAPTMOV(const cam_num_t num); //camera num make a movie
    //Cameras
    void tmk_CMBIMGINT(const cam_num_t num, const quint8 action, const quint8 second); //shorts interval
    void tmk_CMBMOVLEN(const cam_num_t num, const quint8 action, const quint8 second); //set mov length
    void tmk_CMBBRTLEV(const cam_num_t num, const quint8 brightness);
    void tmk_CMBCONLEV(const cam_num_t num, const quint8 contrast);
    void tmk_CMBSATURLEV(const cam_num_t num, const quint8 saturation);
    void tmk_CMBHUELEV(const cam_num_t num, const quint16 hue);
    void tmk_CMBWBTLEV(const cam_num_t num, const quint16 temperature);
    void tmk_CMBGAMLEV(const cam_num_t num, const quint8 gamma);
    void tmk_CMBSHARPLEV(const cam_num_t num, const quint8 sharpness);
    void tmk_CMBBCLEV(const cam_num_t num, const quint8 backlight_compensation);
    void tmk_CMBEXPLEV(const cam_num_t num, const quint16 exposure);
    void tmk_CMBFOCUSLEV(const cam_num_t num, const quint16 focus);
    void tmk_CMBCAMSETCNF(const cam_num_t num);
    void tmk_CMBCAMSETRST(const cam_num_t num);

    void tmk_CMBMEDCIRT(const bc_num_t num, const quint8 second); //set media circulation time
    void tmk_CMBMEDCIRTCNF(const bc_num_t num); //media circulation confirm (start)
    void tmk_CMBMEDCIRTRST(const bc_num_t num); //media circulation time to default

    void tmk_CMBSTPVMOV(const bc_num_t num, const quint8 dir, const quint8 mmCode); //move stepper

    void tmk_CMBUVTIME(const bc_num_t num, const quint16 UV_LED_MSECONDS); //prepare UV LED time in Science mode
    void tmk_CMBUVSETCNF(const bc_num_t num); //confirm UV LED time in Science mode
    void tmk_CMBUVSETRST(const bc_num_t num); //UV LED to default
    void tmk_CMBOBTSET(const quint16 week_number, const quint32 minor_cycle_in_week, const quint8 dummyByte); //OBTime
    void tmk_CMBTCTEMP(const bool force, const quint8 target, const quint16 degree_0_25); //set Target temper prepare
    void tmk_CMBTCSETCNF(const quint8 num); //set Target temper confirm
    void tmk_CMBTXPREP(const quint8 interface); //prepate DATA for transmitting
    void tmk_CMBDATATX(const quint8 interface); //interface 0~4 //transmit or stop transmitting data IDHU/1553
    void tmk_CMBBUFDATATX(const quint8 INTERFASO_BUF); //transmit data from buffer
    void tmk_CMBDATACLRPREP(const quint8 interface); //prepare to clear
    void tmk_CMBDATACLRCNF(const quint8 interface); //clear data confirm

    void tmk_CMBPRINT(const quint8 num);
    void tmk_CMBPRINTCNF(const quint8 num);

    void tmk_CMBMEDCIR(const bc_num_t num); //prepare media circulation
    void tmk_CMBMEDCIRCNF(const bc_num_t num); //confirm media circulation

    void tmk_CMBTLMPREP(); //prepare TMetry table for 1553
    void tmk_CMBTLMGET(); //get TMetry table for 1553
    void tmk_CMBUPDATETIME(const quint8 action); //prepare to set TMetry table update time (default 32s)
    void tmk_CMBTLMSETCNF(); //confirm to set TMetry table update time (default 32s)
    void tmk_CMBTLMCNTRST(); //prepare to reset all telemetry counters
    void tmk_CMBTLMCNTCNF(); //confirm to reset all telemetry counters

    //Non-protocol
    void tmk_CMBFANPWR(const fn_num_t num, const on_off_state_t state);
signals:
    void newTeleMetryTableReceived(const std::array<quint16, RT_Subaddr_BCN_TM_LEN>);
};
