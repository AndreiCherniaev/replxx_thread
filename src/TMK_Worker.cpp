#include "TMK_Worker.h"
#include "tmk_common.h"
#include "myTypes.h"
#include "functypes.h"
#include <sys/stat.h>

//For TA1-USB-01-C
#define BC_DEVICE_NUM ( 0 ) //PCI==0, USB==1 (check using ls -l /dev | grep -a "tmk")
#define BC_BASE_NUM ( 00 ) //current base number

//#define MY_DATA_LEN ( 32 ) //1 means 1 word, 2 means 2 words, ..., 31 means 31 words, 32 means (32 & 0x1F) = 00000 (32 слова кодируется нулём!)
#define BC_BUF_SIZE ( 64 ) //база - участок длинной 64 слова

TMK_Worker::TMK_Worker(QObject *parent) :
    QObject(parent)
    ,TMtable_pending(new QSemaphore())
{
    if(tmk_BC_init()!= -1){
        isCommunicable= true;
        //tmk_RT_transmit_BC(RT_Subaddr_BCN_TM, RT_Subaddr_BCN_TM_LEN);
        //tmk_RT_transmit_BC(RT_Subaddr_RECEIPT, RT_Subaddr_RECEIPT_LEN);
        //tmk_CMBADCREAD(4);//tmk_CMBFANPWR(fn1, s_on);
    }else{
        qDebug() << "no more work with TA1-USB-01-C";
    }
}


TMK_Worker::~TMK_Worker()
{
    int res= tmkdone(0); //free device 0
    if(res== TMK_OK) qDebug() << "tmkdone() ok";
    else qDebug() << "tmkdone() err with code" << res;

    TmkClose();
    qDebug() << "TmkClose(). finish";
}


//from Linux/source/tmk/tmk.cpp
//fWaitTime [mSeconds] wating time where 0 means no wating, -1 means infinite data waiting
void TMK_Worker::CheckTmkEvent(const int fWaitTime)
{
 TTmkEventData tmkEvD;
 int nSaveTmk;
 int nTmk;
 int hTMK = 0;
 unsigned int dwEvent;
 dwEvent = tmkwaitevents(1<<BC_DEVICE_NUM, fWaitTime);//infinite loop

 //#define ERRLEN 40
 uint16_t bc_rw=0xFFFF; /* слово состояния обмена */
 uint16_t bc_aw1=0xFFFF;/* первое ответное слово, если bc_sw != 0 */
 //no need//uint16_t bc_aw2=0xFFFF;/* второе ответное слово, если bc_sw != 0 */

 for(; dwEvent != 0; hTMK++, dwEvent = dwEvent >> 1)
 {
    if((dwEvent & 0x01) == 0) continue;
    while(1)
    {
    tmkgetevd(&tmkEvD);
    if(tmkEvD.nInt == 0) //if no err then
        break; //go out from this while(1)

    //qDebug() << "nInt =" << tmkEvD.nInt; //page 14
    switch(tmkEvD.wMode){
    case BC_MODE:
    bc_rw = tmkEvD.bc.wResult;
    if(tmkEvD.nInt == 1){
        if(bc_rw== 0) qDebug() << "wResult == 0 (status word is zero)"; //<< "wResult == 0 (слово результата обмена равно нулю)";
        else qDebug() << "err wResult == " << bc_rw << "(status word != zero)"; //<< "(слово результата обмена != нулю)";
        //qDebug() << "bcgetstate()= " << bcgetstate(); //возвращает двойное слово, младшие 16 разрядов которого содержат номер базы, с которой происходит работа, а старшие 16 разрядов - текущее слово состояния

        bc_TransmitCommand_t tw;
        qDebug().noquote().nospace() << parseTransmitCommand(bcgetw(0), tw);

        if(tw.K_from_Transmit_command== (bool)RT_TRANSMIT){
            TMtable_pending->tryAcquire(1); //no more wating TMtable
            bc_statusWord_t sw;
            qDebug().noquote().nospace() << "Status Word= bcgetw(1)= " << parse_BC_StatusWord(bcgetw(1), sw); //Ответное Слово (ОС)
            //const auto mess_RT_addr = sw.addr_from_StatusWord; //RT (which transmit current message) address
            //const auto mess_RT_subAddr = tw.SA_SI_from_Transmit_command; //RT (which transmit current message) subAddr
            //const quint32 mess_size = ( 1+ tw.N_COP_from_Transmit_command +1 ); //[words] //Transmit comand(1)+Data Words[1..32]+Status Word(1)
            //const quint32 payloadDataSize= tw.N_COP_from_Transmit_command; //[words]
            std::array<quint16, RT_Subaddr_BCN_TM_LEN> bcgetw_arr;
            bcgetblk(2, (void*)bcgetw_arr.data(), bcgetw_arr.size());
            //Uncomment to watch what inside array
            //for(quint32 i=0;i<bcgetw_arr.size();i++){
            //    qDebug("item=%02u, val=0x%04X", i, bcgetw_arr.at(i));
            //}
            emit newTeleMetryTableReceived(bcgetw_arr);
        }
        ++good_starts;
    }else{
        //If you are here means some 1553 CMD fails and it is not TMtable
        bc_aw1 = tmkEvD.bc.wAW1;
        /*tmkEvD.bc.wAW2 not need because we don't use RT-RT mode at BCN
        qDebug() << "bad_starts num " << bad_starts << " wResult=" << bc_rw << "bc_aw1" << bc_aw1 << "bc_aw2" << bc_aw2;*/
        if(bc_rw & IB_MASK){
            bc_statusWord_t sw;
            parse_BC_StatusWord(tmkEvD.bc.wAW1, sw);
            if(sw.StatusWord_E_bit){
                if(!TMtable_pending->tryAcquire(1)){
                    qDebug().noquote() << "bad_starts num " << bad_starts << " wResult=" << bc_rw << "bc_aw1" << bc_aw1 << bc_out_sw(bc_rw) << parse_BC_StatusWord(tmkEvD.bc.wAW1, sw);
                    ++bad_starts;
                }
            }else{
                qDebug().noquote() << "bad_starts num " << bad_starts << " wResult=" << bc_rw << "bc_aw1" << bc_aw1 << bc_out_sw(bc_rw) << parse_BC_StatusWord(tmkEvD.bc.wAW1, sw);
                ++bad_starts;
            }
            //tmkEvD.bc.wAW2 not need because we don't use RT-RT mode at BCN
        }else{
            qDebug().noquote() << "bad_starts num " << bad_starts << " wResult=" << bc_rw << "bc_aw1" << bc_aw1 << bc_out_sw(bc_rw); //parse and print errors
            ++bad_starts;
        }
    }
    break;
    }
    }//while(1)

   //qDebug() << "good_starts=" << good_starts << "bad_starts=" << bad_starts;
}//for all TMKs
}


int TMK_Worker::tmk_BC_init(){
    TMK_DATA_RET curr_mode= UNDEFINED_MODE;
    int nSaveTmk= 0;
    auto myBus= BUS_A; //here you can set bus
    int res= TmkOpen();
    if(res== TMK_OK) qDebug() << "TmkOpen() ok";
    else{
        //works when (0644/crw-r--r--) but default is (0600/crw-------)
        // crw-r--r--   1 root root    180,     1 Jan 16 19:31 tmk1553busb1
        //if you change kernel version then read readme_CherniaevComment.md , summary
        //cd ${MyBaseDirBCN}/Driver_TA1-USB/ta1usblin0109m/tmk1553busb_0109m/
        //chmod +x make30m
        //./make30m
        struct stat st;
        if(stat("/dev/tmk1553busb1", &st)!= -1){
            qDebug() << "1)check that you have only one device tmk1553busb1: \nls -l /dev | grep -a \"tmk\"";
            if(st.st_mode!= 0x21A4){
                qDebug("2)stat /dev/tmk1553busb1 return mode==%o, please change to 644:", st.st_mode & 0x1FF);
                qDebug() << "sudo chmod ugo+r /dev/tmk1553busb1";
            }
        }else{
            qDebug() << "TmkOpen() err with code" << res << "check:\nls -l /dev | grep -a \"tmk\"\nUSE\nsudo insmod ${MyBaseDirBCN}/Driver_TA1-USB/ta1usblin0109m/tmk1553busb_0109m/tmk1553busb.ko chmod=0666\n";
        }

        goto FINISH_LABEL;
    }

    res= tmkconfig(BC_DEVICE_NUM); //leaves after themself that (arg) device selected
    if(res== TMK_OK) qDebug() << "tmkconfig() ok";
    else{
        qDebug() << "tmkconfig() err with code" << res;
        goto FINISH_LABEL;
    }
    /*Для нескольких устройств потребуется их переключение с
    помощью tmkselect. При этом состояние невыбранных устройств не изменяется.*/
    nSaveTmk = tmkselected();
    qDebug() << "selected device # " << nSaveTmk;

    res= bcreset(); //set our device to be Bus Controller (BC_MODE)
    if(res== TMK_OK) qDebug() << "bcreset() with NUM=" << bcgetbus() << "ok";
    else{
        qDebug() << "bcreset() err with code" << res;
        goto FINISH_LABEL;
    }

    res= bcdefbus(myBus);
    if(res== TMK_OK){
        if(myBus== BUS_A) qDebug() << "BUS_A (Main)";
        else qDebug() << "BUS_B (Redundant)";
    }else if(res== BC_BAD_BUS){
        qDebug() << "bcdefbus() err BC_BAD_BUS";
        goto FINISH_LABEL;
    }else{
        qDebug() << "bcdefbus() err unknown";
        goto FINISH_LABEL;
    }

    curr_mode= tmkgetmode();
    parseMode(curr_mode);


    /*Для устройств TA, TAI, MRTA, MRTAI можно настраивать таймаут ожидания
    ответного слова. Для этого введена функция tmktimeout. В качестве параметра задается
    либо требуемое значение таймаута в мкс, либо константа GET_TIMEOUT для чтения
    текущего значения. Если задано значение таймаута, драйвер преобразует его к одному
    из аппаратно поддерживаемых значений, не меньше заданного. Функция возвращает
    текущее установленное значение в мкс или 0, если функция не поддерживается на
    текущем выбранном устройстве.*/
    //Uncomment to get more info
    //qDebug() << "GET_TIMEOUT==" << tmktimeout(GET_TIMEOUT) << "us";
    //Uncomment to get more info
    //qDebug() << "bcgetmaxbase= " << bcgetmaxbase();

    res= bcdefbase(BC_BASE_NUM);  //БАЗА in range [0 .. bcgetmaxbase()]
    if(res== TMK_OK){
        qDebug() << "bcdefbase() res= " << res;
        if(bcgetbase()== BC_BASE_NUM){
            qDebug() << "bcdefbase() set BASE to" << BC_BASE_NUM << "ok";
        }
    }else{
        qDebug() << "bcdefbase() err with code" << res;
        goto FINISH_LABEL;
    }



    return res;

FINISH_LABEL:;
    TmkClose();
    qDebug() << "TmkClose(). finish";
    return res;
}


//BC transmit to RT to TURN ON/OFF FAN 1~2
void TMK_Worker::tmk_CMBFANPWR(const fn_num_t num, const on_off_state_t state){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    quint8 my_data_len_Bytes= 3; //for CMBFANPWR
    quint8 my_data_len_Words= (my_data_len_Bytes % 2)? (my_data_len_Bytes/2)+1 : (my_data_len_Bytes/2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)FAN_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBPUMPPWR(const pump_num_t num, const on_off_state_t state){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBPUMPPWR_TotalArgLen/ 2)+(CMBPUMPPWR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBPUMPPWR_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


////to debug two cmds in one stream
//void TMK_Worker::tmk_CMBCAMPWR(const quint8 num, const on_off_state_t state){
//    TMK_DATA myComandWord= 0;
//    int res= -1;
//    //Calc buf[] size
//    const quint8 my_data_len_Words= (CMBCAMPWR_TotalArgLen/ 2)+(CMBCAMPWR_TotalArgLen% 2);
//    quint16 buf[1+my_data_len_Words+my_data_len_Words]; //myComandWord+DataWords

//    //BC transmit to RT
//    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words+my_data_len_Words);
//    buf[0]= myComandWord;
//    buf[1]= (quint16)CMBCAMPWR_tmk_code<<8 | (quint8)num;
//    buf[2]= ((quint16)(quint8)state)<<8;
//    buf[3]= (quint16)CMBCAMPWR_tmk_code<<8 | (quint8)2;
//    buf[4]= ((quint16)(quint8)state)<<8;

//    bcputblk(0, buf, elmof(buf));
//    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
//    if(res!= TMK_OK) qDebug() << "err bcstart()";
//    else qDebug() << "bcstart() ok";

//    CheckTmkEvent(-1);
//}


void TMK_Worker::tmk_CMBCAMPWR(const quint8 num, const on_off_state_t state){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBCAMPWR_TotalArgLen/ 2)+(CMBCAMPWR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBCAMPWR_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBHTRPWR(const htr_num_t num, const on_off_state_t state){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBHTRPWR_TotalArgLen/ 2)+(CMBHTRPWR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBHTRPWR_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBSTPPWR(const bc_num_t num, const on_off_state_t state){ //stepper
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBSTPPWR_TotalArgLen/ 2)+(CMBSTPPWR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBSTPPWR_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBSBPWR(const sb_num_t num, const on_off_state_t state){ //SensorBoard
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBSBPWR_TotalArgLen/ 2)+(CMBSBPWR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBSBPWR_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTCSPWR(const tc_set_num_t num, const on_off_state_t state){ //THERMAL COUPLE set
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTCSPWR_TotalArgLen/ 2)+(CMBTCSPWR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTCSPWR_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBUVPWR(const uv_num_t num, const on_off_state_t state){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBUVPWR_TotalArgLen/ 2)+(CMBUVPWR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBUVPWR_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)state<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBOFF(){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBOFF_TotalArgLen/ 2)+(CMBOFF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBOFF_tmk_code<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBOFFCNF(){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBOFFCNF_TotalArgLen/ 2)+(CMBOFFCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBOFFCNF_tmk_code<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBCAPTIMG(const cam_num_t num, const quint8 shorts_amo){ //camera num captures some photos
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBCAPTIMG_TotalArgLen/ 2)+(CMBCAPTIMG_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBCAPTIMG_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)shorts_amo<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBCAPTMOV(const cam_num_t num){//camera num make a movie (for movie length see CMBMOVLEN)
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBCAPTMOV_TotalArgLen/ 2)+(CMBCAPTMOV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBCAPTMOV_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBIMGINT(const cam_num_t num, const quint8 action, const quint8 second){ //shorts interval
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBIMGINT_TotalArgLen/ 2)+(CMBIMGINT_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBIMGINT_tmk_code<<8 | (quint8)num;
    buf[2]= ((quint16)action)<<8 | (quint8)second;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBMOVLEN(const cam_num_t num, const quint8 sign, const quint8 second){ //set mov length
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBMOVLEN_TotalArgLen/ 2)+(CMBMOVLEN_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBMOVLEN_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)sign<<8 | (quint8)second;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBBRTLEV(const cam_num_t num, const quint8 brightness){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBBRTLEV_TotalArgLen/ 2)+(CMBBRTLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBBRTLEV_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)brightness<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}



void TMK_Worker::tmk_CMBCONLEV(const cam_num_t num, const quint8 contrast){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBCONLEV_TotalArgLen/ 2)+(CMBCONLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBCONLEV_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)contrast<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBSATURLEV(const cam_num_t num, const quint8 saturation){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBSATURLEV_TotalArgLen/ 2)+(CMBSATURLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBSATURLEV_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)saturation<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBHUELEV(const cam_num_t num, const quint16 hue){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBHUELEV_TotalArgLen/ 2)+(CMBHUELEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBHUELEV_tmk_code<<8 | (quint8)num;
    buf[2]= hue;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBWBTLEV(const cam_num_t num, const quint16 temperature){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBWBTLEV_TotalArgLen/ 2)+(CMBWBTLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBWBTLEV_tmk_code<<8 | (quint8)num;
    buf[2]= temperature;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBGAMLEV(const cam_num_t num, const quint8 gamma){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBGAMLEV_TotalArgLen/ 2)+(CMBGAMLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBGAMLEV_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)gamma<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBSHARPLEV(const cam_num_t num, const quint8 sharpness){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBSHARPLEV_TotalArgLen/ 2)+(CMBSHARPLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBSHARPLEV_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)sharpness<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBBCLEV(const cam_num_t num, const quint8 backlight_compensation){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBBCLEV_TotalArgLen/ 2)+(CMBBCLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBBCLEV_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)backlight_compensation<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBEXPLEV(const cam_num_t num, const quint16 exposure){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBEXPLEV_TotalArgLen/ 2)+(CMBEXPLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBEXPLEV_tmk_code<<8 | (quint8)num;
    buf[2]= exposure;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBFOCUSLEV(const cam_num_t num, const quint16 focus){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBFOCUSLEV_TotalArgLen/ 2)+(CMBFOCUSLEV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBFOCUSLEV_tmk_code<<8 | (quint8)num;
    buf[2]= focus;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBCAMSETCNF(const cam_num_t num){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBCAMSETCNF_TotalArgLen/ 2)+(CMBCAMSETCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBCAMSETCNF_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBCAMSETRST(const cam_num_t num){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBCAMSETRST_TotalArgLen/ 2)+(CMBCAMSETRST_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBCAMSETRST_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBMEDCIRT(const bc_num_t num, const quint8 second){ //prepare pump time for media only
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBMEDCIRT_TotalArgLen/ 2)+(CMBMEDCIRT_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBMEDCIRT_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)second<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBMEDCIRTCNF(const bc_num_t num){ //media circulation confirm (start)
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBMEDCIRTCNF_TotalArgLen/ 2)+(CMBMEDCIRTCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBMEDCIRTCNF_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBMEDCIRTRST(const bc_num_t num){ //media circulation time to default
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBMEDCIRTRST_TotalArgLen/ 2)+(CMBMEDCIRTRST_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBMEDCIRTRST_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBSTPVMOV(const bc_num_t num, const quint8 dir, const quint8 mmCode){ //move stepper
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBSTPVMOV_TotalArgLen/ 2)+(CMBSTPVMOV_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBSTPVMOV_tmk_code<<8 | (quint8)num;
    buf[2]= (quint16)dir<<8 | (quint8)mmCode;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBUVTIME(const bc_num_t num, const quint16 UV_LED_MSECONDS){ //prepare UV LED time in Science mode
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBUVTIME_TotalArgLen/ 2)+(CMBUVTIME_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBUVTIME_tmk_code<<8 | (quint8)num;
    buf[2]= UV_LED_MSECONDS;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}

void TMK_Worker::tmk_CMBUVSETCNF(const bc_num_t num){ //confirm UV LED time in Science mode
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBUVSETCNF_TotalArgLen/ 2)+(CMBUVSETCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBUVSETCNF_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBUVSETRST(const bc_num_t num){ //UV LED to default
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBUVSETRST_TotalArgLen/ 2)+(CMBUVSETRST_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBUVSETRST_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBOBTSET(const quint16 week_number, const quint32 minor_cycle_in_week, const quint8 dummyByte){ //OBTime
    TMK_DATA myComandWord= 0;
    const uint8_16_t week_number_tmp= {.u16= week_number};
    const uint8_32_t minor_cycle_in_week_tmp= {.u32= minor_cycle_in_week};
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBOBTSET_TotalArgLen/ 2)+(CMBOBTSET_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBOBTSET_tmk_code<<8 | (quint8)week_number_tmp.u8s[1];
    buf[2]= (quint16)week_number_tmp.u8s[0]<<8 | (quint8)minor_cycle_in_week_tmp.u8s[3];
    buf[3]= (quint16)minor_cycle_in_week_tmp.u8s[2]<<8 | (quint8)minor_cycle_in_week_tmp.u8s[1];
    buf[4]= (quint16)minor_cycle_in_week_tmp.u8s[0]<<8 | (quint8)dummyByte;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTCTEMP(const bool force, const quint8 target, const quint16 degree_0_25){ //(const quint8 num, const quint8 level, const quint8 degree){ //set Target temper prepare
    TMK_DATA myComandWord= 0;
    int res= -1;
    const uint8_16_t degree_0_25_tmp= {.u16= degree_0_25};
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTCTEMP_TotalArgLen/ 2)+(CMBTCTEMP_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTCTEMP_tmk_code<<8 | (force? (quint8)((1<<6)|target) : target);
    buf[2]= (quint16)degree_0_25_tmp.u8s[1]<<8 | degree_0_25_tmp.u8s[0];

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTCSETCNF(const quint8 num){ //set Target temper confirm
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTCSETCNF_TotalArgLen/ 2)+(CMBTCSETCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTCSETCNF_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTXPREP(const quint8 interface){ //prepate DATA for transmitting
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTXPREP_TotalArgLen/ 2)+(CMBTXPREP_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTXPREP_tmk_code<<8 | (quint8)interface;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBDATATX(const quint8 interface){ //interface 0~4 //transmit or stop transmitting data IDHU/1553
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBDATATX_TotalArgLen/ 2)+(CMBDATATX_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBDATATX_tmk_code<<8 | (quint8)interface;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}

void TMK_Worker::tmk_CMBBUFDATATX(const quint8 INTERFASO_BUF){ //transmit data from buffer
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBBUFDATATX_TotalArgLen/ 2)+(CMBBUFDATATX_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBBUFDATATX_tmk_code<<8 | (quint8)INTERFASO_BUF;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}

void TMK_Worker::tmk_CMBDATACLRPREP(const quint8 interface){ //prepare to clear
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBDATACLRPREP_TotalArgLen/ 2)+(CMBDATACLRPREP_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBDATACLRPREP_tmk_code<<8 | (quint8)interface;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBDATACLRCNF(const quint8 interface){ //clear data confirm
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBDATACLRCNF_TotalArgLen/ 2)+(CMBDATACLRCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBDATACLRCNF_tmk_code<<8 | (quint8)interface;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBPRINT(const quint8 num){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBPRINT_TotalArgLen/ 2)+(CMBPRINT_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBPRINT_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBPRINTCNF(const quint8 num){
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBPRINTCNF_TotalArgLen/ 2)+(CMBPRINTCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBPRINTCNF_tmk_code<<8 | num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBMEDCIR(const bc_num_t num){ //prepare media circulation
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBMEDCIR_TotalArgLen/ 2)+(CMBMEDCIR_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBMEDCIR_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}

void TMK_Worker::tmk_CMBMEDCIRCNF(const bc_num_t num){ //confirm media circulation
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBMEDCIRCNF_TotalArgLen/ 2)+(CMBMEDCIRCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBMEDCIRCNF_tmk_code<<8 | (quint8)num;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTLMPREP(){ //prepare TMetry table for 1553
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTLMPREP_TotalArgLen/ 2)+(CMBTLMPREP_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTLMPREP_tmk_code<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}

void TMK_Worker::tmk_CMBTLMGET(){ //get TMetry table for 1553
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTLMGET_TotalArgLen/ 2)+(CMBTLMGET_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTLMGET_tmk_code<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBUPDATETIME(const quint8 action){ //prepare to set TMetry table update time (default 32s)
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBUPDATETIME_TotalArgLen/ 2)+(CMBUPDATETIME_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBUPDATETIME_tmk_code<<8 | (quint8)action;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTLMSETCNF(){ //confirm to set TMetry table update time (default 32s)
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTLMSETCNF_TotalArgLen/ 2)+(CMBTLMSETCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTLMSETCNF_tmk_code<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTLMCNTRST(){ //prepare to reset all telemetry counters
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTLMCNTRST_TotalArgLen/ 2)+(CMBTLMCNTRST_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTLMCNTRST_tmk_code<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


void TMK_Worker::tmk_CMBTLMCNTCNF(){ //confirm to reset all telemetry counters
    TMK_DATA myComandWord= 0;
    int res= -1;
    //Calc buf[] size
    const quint8 my_data_len_Words= (CMBTLMCNTCNF_TotalArgLen/ 2)+(CMBTLMCNTCNF_TotalArgLen% 2);
    quint16 buf[1+my_data_len_Words]; //myComandWord+DataWords

    //BC transmit to RT
    myComandWord = CW(BCN_RT_ADDRESS, RT_RECEIVE, RT_Subaddr_CONTROL_COMMAND, my_data_len_Words);
    buf[0]= myComandWord;
    buf[1]= (quint16)CMBTLMCNTCNF_tmk_code<<8;

    bcputblk(0, buf, elmof(buf));
    res= bcstart(BC_BASE_NUM, DATA_BC_RT); //код управления "передача данных КК-ОУ (формат 1)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";
    else qDebug() << "bcstart() ok";

    CheckTmkEvent(-1);
}


//RT transmit to BC len words
void TMK_Worker::tmk_RT_transmit_BC(const TMK_DATA subaddr, const quint8 len){
    TMK_DATA myComandWord=  0;
    TMK_DATA realComandWord=  0; //this is still myComandWord read back from hardware. if myComandWord==realComandWord then ok else unknown err
    int res= -1;

    Q_ASSERT(len<= tmk_pack_max_len_words);
    myComandWord= CW(BCN_RT_ADDRESS, RT_TRANSMIT, subaddr, len); //00000(двоичн.) задает длину блока в 32 слова //see also "Command Word Bit Usage" https://en.wikipedia.org/wiki/MIL-STD-1553
    //  uncomment if you want debuging (compare ДОЗУ before writing and after writing)
    //  //clear ДОЗУ in база 0
    //  for(quint32 i=0;i<BC_BUF_SIZE;i++) bcputw(i, 0);
    bcputw(0, myComandWord);
    //Uncomment to debug
    /*realComandWord= bcgetw(0);
    if(myComandWord==realComandWord) qDebug("myComandWord= 0x%04X", myComandWord);
    else qDebug("err myComandWord=0x%04X but put value=0x%04X", myComandWord, realComandWord);*/
    res= bcstart(BC_BASE_NUM, DATA_RT_BC); //код управления "передача данных ОУ-КК (формат 2)" tmkguide.doc page 39
    if(res!= TMK_OK) qDebug() << "err bcstart()";

    CheckTmkEvent(-1);
}


//RT transmit to BC TeleMetryTable
void TMK_Worker::tmk_RT_transmit_BC_TeleMetryTable(){
    tmk_RT_transmit_BC(RT_Subaddr_BCN_TM, RT_Subaddr_BCN_TM_LEN);
    if(!TMtable_pending->available()) TMtable_pending->release(1);
}


//void TMK_Worker::tmk_CMBFANPWR(){
//    QList<quint16> cmd;
//    cmd.append(0x0100);
//    cmd.append(0x0000);
//    tmk_RT_receive_BC(RT_Subaddr_CONTROL_COMMAND, cmd);
//}
