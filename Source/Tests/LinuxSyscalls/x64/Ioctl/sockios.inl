#ifndef SIOCGSTAMP_OLD
#define SIOCGSTAMP_OLD 0x8906
#endif
_BASIC_META(SIOCGSTAMP_OLD)
#ifndef SIOCGSTAMPNS_OLD
#define SIOCGSTAMPNS_OLD 0x8907
#endif
_BASIC_META(SIOCGSTAMPNS_OLD)
_BASIC_META(SIOCADDRT)
_BASIC_META(SIOCDELRT)
_BASIC_META(SIOCRTMSG)
_BASIC_META(SIOCGIFNAME)
_BASIC_META(SIOCSIFLINK)
_CUSTOM_META(SIOCGIFCONF,
  _IOC(
    _IOC_DIR(SIOCGIFCONF),
    _IOC_TYPE(SIOCGIFCONF),
    _IOC_NR(SIOCGIFCONF),
    sizeof(struct ifconf))) // This should hit failure
_BASIC_META(SIOCGIFFLAGS)
_BASIC_META(SIOCSIFFLAGS)
_BASIC_META(SIOCGIFADDR)
_BASIC_META(SIOCSIFADDR)
_BASIC_META(SIOCGIFDSTADDR)
_BASIC_META(SIOCSIFDSTADDR)
_BASIC_META(SIOCGIFBRDADDR)
_BASIC_META(SIOCSIFBRDADDR)
_BASIC_META(SIOCGIFNETMASK)
_BASIC_META(SIOCSIFNETMASK)
_BASIC_META(SIOCGIFMETRIC)
_BASIC_META(SIOCSIFMETRIC)
_BASIC_META(SIOCGIFMEM)
_BASIC_META(SIOCSIFMEM)
_BASIC_META(SIOCGIFMTU)
_BASIC_META(SIOCSIFMTU)
_BASIC_META(SIOCSIFNAME)
_BASIC_META(SIOCSIFHWADDR)
_BASIC_META(SIOCGIFENCAP)
_BASIC_META(SIOCSIFENCAP)
_BASIC_META(SIOCGIFHWADDR)
_BASIC_META(SIOCGIFSLAVE)
_BASIC_META(SIOCSIFSLAVE)
_BASIC_META(SIOCADDMULTI)
_BASIC_META(SIOCDELMULTI)
_BASIC_META(SIOCGIFINDEX)
_BASIC_META(SIOCSIFPFLAGS)
_BASIC_META(SIOCGIFPFLAGS)
_BASIC_META(SIOCDIFADDR)
_BASIC_META(SIOCSIFHWBROADCAST)
_BASIC_META(SIOCGIFCOUNT)
_BASIC_META(SIOCGIFBR)
_BASIC_META(SIOCSIFBR)
_BASIC_META(SIOCGIFTXQLEN)
_BASIC_META(SIOCSIFTXQLEN)
_BASIC_META(SIOCETHTOOL)
_BASIC_META(SIOCGMIIPHY)
_BASIC_META(SIOCGMIIREG)
_BASIC_META(SIOCSMIIREG)
_BASIC_META(SIOCWANDEV)
_BASIC_META(SIOCOUTQNSD)
_BASIC_META(SIOCGSKNS)
_BASIC_META(SIOCDARP)
_BASIC_META(SIOCGARP)
_BASIC_META(SIOCSARP)
_BASIC_META(SIOCDRARP)
_BASIC_META(SIOCGRARP)
_BASIC_META(SIOCSRARP)
_BASIC_META(SIOCGIFMAP)
_BASIC_META(SIOCSIFMAP)
_BASIC_META(SIOCADDDLCI)
_BASIC_META(SIOCDELDLCI)
_BASIC_META(SIOCGIFVLAN)
_BASIC_META(SIOCSIFVLAN)
_BASIC_META(SIOCBONDENSLAVE)
_BASIC_META(SIOCBONDRELEASE)
_BASIC_META(SIOCBONDSETHWADDR)
_BASIC_META(SIOCBONDSLAVEINFOQUERY)
_BASIC_META(SIOCBONDINFOQUERY)
_BASIC_META(SIOCBONDCHANGEACTIVE)
_BASIC_META(SIOCBRADDBR)
_BASIC_META(SIOCBRDELBR)
_BASIC_META(SIOCBRADDIF)
_BASIC_META(SIOCBRDELIF)
_BASIC_META(SIOCSHWTSTAMP)
_BASIC_META(SIOCGHWTSTAMP)

_CUSTOM_META_OFFSET(SIOCDEVPRIVATE,   SIOCDEVPRIVATE, 0x0)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_1, SIOCDEVPRIVATE, 0x1)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_2, SIOCDEVPRIVATE, 0x2)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_3, SIOCDEVPRIVATE, 0x3)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_4, SIOCDEVPRIVATE, 0x4)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_5, SIOCDEVPRIVATE, 0x5)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_6, SIOCDEVPRIVATE, 0x6)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_7, SIOCDEVPRIVATE, 0x7)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_8, SIOCDEVPRIVATE, 0x8)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_9, SIOCDEVPRIVATE, 0x9)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_A, SIOCDEVPRIVATE, 0xA)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_B, SIOCDEVPRIVATE, 0xB)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_C, SIOCDEVPRIVATE, 0xC)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_D, SIOCDEVPRIVATE, 0xD)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_E, SIOCDEVPRIVATE, 0xE)
_CUSTOM_META_OFFSET(SIOCDEVPRIVATE_F, SIOCDEVPRIVATE, 0xF)

_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE,   SIOCPROTOPRIVATE, 0x0)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_1, SIOCPROTOPRIVATE, 0x1)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_2, SIOCPROTOPRIVATE, 0x2)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_3, SIOCPROTOPRIVATE, 0x3)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_4, SIOCPROTOPRIVATE, 0x4)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_5, SIOCPROTOPRIVATE, 0x5)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_6, SIOCPROTOPRIVATE, 0x6)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_7, SIOCPROTOPRIVATE, 0x7)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_8, SIOCPROTOPRIVATE, 0x8)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_9, SIOCPROTOPRIVATE, 0x9)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_A, SIOCPROTOPRIVATE, 0xA)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_B, SIOCPROTOPRIVATE, 0xB)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_C, SIOCPROTOPRIVATE, 0xC)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_D, SIOCPROTOPRIVATE, 0xD)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_E, SIOCPROTOPRIVATE, 0xE)
_CUSTOM_META_OFFSET(SIOCPROTOPRIVATE_F, SIOCPROTOPRIVATE, 0xF)
