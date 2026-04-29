/* FUN_ffffff80022661a6 @ ffffff80022661a6 */

void FUN_ffffff80022661a6(long param_1,int param_2)

{
  long lVar1;
  long lVar2;
  undefined2 uVar3;
  undefined2 uVar4;
  bool bVar5;

  lVar2 = *(long *)(param_1 + 0x10);
  *(int *)(lVar2 + 0x148) = param_2;
  bVar5 = param_2 == 1;
  uVar3 = 0x46;
  if (bVar5) {
    uVar3 = 0xff9c;
  }
  uVar4 = 0xffbf;
  if (bVar5) {
    uVar4 = 0xff80;
  }
  *(uint *)(lVar2 + 0x140) = (uint)bVar5 | *(uint *)(lVar2 + 0x140) & 0xfffffffe;
  *(undefined4 *)(lVar2 + 0x448) = 0x32ffbf;
  *(undefined2 *)(lVar2 + 0x44e) = uVar3;
  *(undefined2 *)(lVar2 + 0x44c) = uVar4;
  lVar1 = *(long *)(lVar2 + 8);
  if (lVar1 != 0) {
    *(undefined8 *)(*(long *)(lVar1 + 0x10) + 8) = *(undefined8 *)(lVar2 + 0x448);
    *(undefined1 *)(*(long *)(lVar1 + 0x10) + 0x20) = 1;
    lVar2 = *(long *)(param_1 + 0x10);
  }
  if (*(long *)(lVar2 + 0x440) != 0) {
                    /* WARNING: Subroutine does not return */
    FUN_ffffff8003222728(*(long *)(lVar2 + 0x440),0);
  }
  return;
}



/* MISSING @ 0xffffff80022662fe */

/* IO80211BssManager::isAssociatedOnHighBand() @ ffffff80022665a0 */

byte IO80211BssManager__isAssociatedOnHighBand__(void)

{
  byte bVar1;

  bVar1 = IO80211BssManager__isAssociatedOn2G__();
  return bVar1 ^ 1;
}



/* IO80211BssManager::isAssociatedOn2G() @ ffffff80022665ae */

undefined8 IO80211BssManager__isAssociatedOn2G__(long param_1)

{
  undefined4 uVar1;
  undefined8 uVar2;
  ulong uVar3;
  undefined8 local_24;
  undefined4 local_1c;
  long local_18;

  local_18 = *(long *)PTR_DAT_ffffff80023cb148;
  if ((long *)**(long **)(param_1 + 0x10) == (long *)0x0) {
    uVar2 = 0;
  }
  else {
    local_1c = 0xaaaaaaaa;
    local_24 = 0xaaaaaaaa00000001;
    uVar1 = (**(code **)(*(long *)**(long **)(param_1 + 0x10) + 0x1e8))();
    uVar2 = FUN_ffffff800211ccf1(uVar1,&local_24);
    uVar3 = CONCAT71((int7)((ulong)uVar2 >> 8),(undefined1)local_1c) & 0xffffffffffffff08;
    uVar2 = CONCAT71((int7)(uVar3 >> 8),(byte)uVar3 >> 3);
  }
  if (*(long *)PTR_DAT_ffffff80023cb148 == local_18) {
    return uVar2;
  }
                    /* WARNING: Subroutine does not return */
  FUN_ffffff8000307340();
}



/* IO80211BssManager::isAssociatedOn5G() @ ffffff8002266624 */

undefined8 IO80211BssManager__isAssociatedOn5G__(long param_1)

{
  undefined4 uVar1;
  undefined8 uVar2;
  ulong uVar3;
  undefined8 local_24;
  undefined4 local_1c;
  long local_18;

  local_18 = *(long *)PTR_DAT_ffffff80023cb148;
  if ((long *)**(long **)(param_1 + 0x10) == (long *)0x0) {
    uVar2 = 0;
  }
  else {
    local_1c = 0xaaaaaaaa;
    local_24 = 0xaaaaaaaa00000001;
    uVar1 = (**(code **)(*(long *)**(long **)(param_1 + 0x10) + 0x1e8))();
    uVar2 = FUN_ffffff800211ccf1(uVar1,&local_24);
    uVar3 = CONCAT71((int7)((ulong)uVar2 >> 8),(undefined1)local_1c) & 0xffffffffffffff10;
    uVar2 = CONCAT71((int7)(uVar3 >> 8),(byte)uVar3 >> 4);
  }
  if (*(long *)PTR_DAT_ffffff80023cb148 == local_18) {
    return uVar2;
  }
                    /* WARNING: Subroutine does not return */
  FUN_ffffff8000307340();
}



/* IO80211BssManager::isAssociatedOn6G() @ ffffff800226669a */

undefined8 IO80211BssManager__isAssociatedOn6G__(long param_1)

{
  undefined4 uVar1;
  undefined8 uVar2;
  ulong uVar3;
  undefined8 local_24;
  undefined4 local_1c;
  long local_18;

  local_18 = *(long *)PTR_DAT_ffffff80023cb148;
  if ((long *)**(long **)(param_1 + 0x10) == (long *)0x0) {
    uVar2 = 0;
  }
  else {
    local_1c = 0xaaaaaaaa;
    local_24 = 0xaaaaaaaa00000001;
    uVar1 = (**(code **)(*(long *)**(long **)(param_1 + 0x10) + 0x1e8))();
    uVar2 = FUN_ffffff800211ccf1(uVar1,&local_24);
    uVar3 = CONCAT71((int7)((ulong)uVar2 >> 8),local_1c._1_1_) & 0xffffffffffffff20;
    uVar2 = CONCAT71((int7)(uVar3 >> 8),(byte)uVar3 >> 5);
  }
  if (*(long *)PTR_DAT_ffffff80023cb148 == local_18) {
    return uVar2;
  }
                    /* WARNING: Subroutine does not return */
  FUN_ffffff8000307340();
}



/* IO80211BssManager::isAssociated() @ ffffff8002266710 */

undefined8 IO80211BssManager__isAssociated__(long param_1)

{
  return CONCAT71((int7)((ulong)*(long **)(param_1 + 0x10) >> 8),**(long **)(param_1 + 0x10) != 0);
}



/* IO80211BssManager::isAssociatedToAdhoc() @ ffffff8002266722 */

bool IO80211BssManager__isAssociatedToAdhoc__(long param_1)

{
  short sVar1;

  if ((long *)**(long **)(param_1 + 0x10) != (long *)0x0) {
    sVar1 = (**(code **)(*(long *)**(long **)(param_1 + 0x10) + 0x2c8))();
    return sVar1 == 1;
  }
  return false;
}



/* IO80211BssManager::setLastBSSRssi() @ ffffff800226682e */

undefined8 IO80211BssManager__setLastBSSRssi__(long param_1)

{
  long lVar1;
  undefined4 uVar2;

  lVar1 = **(long **)(param_1 + 0x10);
  if (lVar1 == 0) {
    uVar2 = 0;
  }
  else {
    uVar2 = *(undefined4 *)(*(long *)(lVar1 + 0x10) + 0x280);
  }
  *(undefined4 *)((long)*(long **)(param_1 + 0x10) + 0x14) = uVar2;
  return 0;
}



/* MISSING @ 0xffffff8002266ac2 */

/* IO80211BssManager::resetRateAndIndexSet() @ ffffff8002266cfc */

void IO80211BssManager__resetRateAndIndexSet__(long param_1)

{
  long lVar1;

  lVar1 = *(long *)(param_1 + 0x10);
  *(undefined8 *)(lVar1 + 0xe0) = 0;
  *(undefined8 *)(lVar1 + 0xd8) = 0;
  *(undefined8 *)(*(long *)(param_1 + 0x10) + 0xe8) = 0;
  *(undefined8 *)(*(long *)(param_1 + 0x10) + 0xf0) = 0;
                    /* WARNING: Subroutine does not return */
  FUN_ffffff8000101100(*(long *)(param_1 + 0x10) + 0x1c,0xbc);
}



/* MISSING @ 0xffffff8002266d68 */

/* IO80211BssManager::isAssociatedToiOSDevice() @ ffffff8002266da8 */

ulong IO80211BssManager__isAssociatedToiOSDevice__(long param_1)

{
  long lVar1;
  ulong uVar2;

  if (**(long **)(param_1 + 0x10) == 0) {
    uVar2 = 0;
  }
  else {
    lVar1 = *(long *)(**(long **)(param_1 + 0x10) + 0x10);
    uVar2 = CONCAT71((int7)((ulong)lVar1 >> 8),*(undefined1 *)(lVar1 + 0x154));
  }
  return uVar2 & 0xffffffffffffff01;
}



/* IO80211BssManager::isAssociatedToLPHSCapableiOSHotspot() @ ffffff8002266e62 */

undefined8 IO80211BssManager__isAssociatedToLPHSCapableiOSHotspot__(long param_1)

{
  long lVar1;
  undefined8 uVar2;

  if (((**(long **)(param_1 + 0x10) == 0) ||
      (lVar1 = *(long *)(**(long **)(param_1 + 0x10) + 0x10), *(char *)(lVar1 + 0x687) == '\0')) ||
     (uVar2 = CONCAT71((int7)((ulong)lVar1 >> 8),1), (*(byte *)(lVar1 + 0x684) & 2) == 0)) {
    uVar2 = 0;
  }
  return uVar2;
}



/* MISSING @ 0xffffff8002266f9a */

/* IO80211BssManager::get6GMode() @ ffffff8002266fcc */

undefined4 IO80211BssManager__get6GMode__(long param_1)

{
  return *(undefined4 *)(*(long *)(param_1 + 0x10) + 0x148);
}



/* FUN_ffffff8002266fee @ ffffff8002266fee */

void FUN_ffffff8002266fee(long param_1,undefined4 param_2)

{
  *(undefined4 *)(*(long *)(param_1 + 0x10) + 0x144) = param_2;
  return;
}



/* IO80211BssManager::isEAPJoin() @ ffffff800226703e */

undefined8 IO80211BssManager__isEAPJoin__(long param_1)

{
  int iVar1;
  undefined8 uVar2;

  iVar1 = *(int *)(*(long *)(param_1 + 0x10) + 0xfc);
  uVar2 = CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),1);
  if (iVar1 < 0x80) {
    if ((iVar1 - 1U < 0x40) && ((0x8000000000000009U >> ((ulong)(iVar1 - 1U) & 0x3f) & 1) != 0)) {
      return uVar2;
    }
  }
  else if (iVar1 < 0x4000) {
    if (iVar1 == 0x80) {
      return uVar2;
    }
    if (iVar1 == 0x800) {
      return uVar2;
    }
  }
  else {
    if (iVar1 == 0x4000) {
      return uVar2;
    }
    if (iVar1 == 0x8000) {
      return uVar2;
    }
  }
  return 0;
}



/* IO80211BssManager::is8021XJoin() @ ffffff800226709a */

undefined8 IO80211BssManager__is8021XJoin__(long param_1)

{
  int iVar1;
  undefined8 uVar2;

  iVar1 = *(int *)(*(long *)(param_1 + 0x10) + 0xfc);
  uVar2 = CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),1);
  if (iVar1 < 0x800) {
    if (iVar1 == 4) {
      return uVar2;
    }
    if (iVar1 == 0x80) {
      return uVar2;
    }
  }
  else {
    if (iVar1 == 0x800) {
      return uVar2;
    }
    if (iVar1 == 0x4000) {
      return uVar2;
    }
    if (iVar1 == 0x8000) {
      return uVar2;
    }
  }
  return 0;
}



/* IO80211BssManager::isDynamicWEP() @ ffffff80022670de */

undefined8 IO80211BssManager__isDynamicWEP__(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(int *)(*(long *)(param_1 + 0x10) + 0xfc) == 0x40);
}



/* FUN_ffffff800226713c @ ffffff800226713c */

undefined8 FUN_ffffff800226713c(long param_1,undefined8 param_2,ulong param_3)

{
  long lVar1;
  undefined8 uVar2;

  uVar2 = 0xe00002db;
  if (param_3 < 0x21) {
    lVar1 = *(long *)(param_1 + 0x10);
    uVar2 = 0;
    *(undefined8 *)(lVar1 + 0x371) = 0;
    *(undefined8 *)(lVar1 + 0x369) = 0;
    *(undefined8 *)(lVar1 + 0x361) = 0;
    *(undefined8 *)(lVar1 + 0x359) = 0;
    lVar1 = *(long *)(param_1 + 0x10);
    *(ulong *)(lVar1 + 0x380) = param_3;
    if (param_3 != 0) {
      FUN_ffffff8000101080(lVar1 + 0x359);
    }
  }
  return uVar2;
}



/* FUN_ffffff80022671de @ ffffff80022671de */

undefined8 FUN_ffffff80022671de(long param_1,undefined8 param_2,ulong param_3)

{
  long lVar1;
  undefined8 uVar2;

  uVar2 = 0xe00002db;
  if (param_3 < 0x21) {
    lVar1 = *(long *)(param_1 + 0x10);
    uVar2 = 0;
    *(undefined8 *)(lVar1 + 0x3a0) = 0;
    *(undefined8 *)(lVar1 + 0x398) = 0;
    *(undefined8 *)(lVar1 + 0x390) = 0;
    *(undefined8 *)(lVar1 + 0x388) = 0;
    lVar1 = *(long *)(param_1 + 0x10);
    *(ulong *)(lVar1 + 0x3a8) = param_3;
    if (param_3 != 0) {
      FUN_ffffff8000101080(lVar1 + 0x388);
    }
  }
  return uVar2;
}



/* FUN_ffffff8002267236 @ ffffff8002267236 */

undefined8 FUN_ffffff8002267236(long param_1,undefined8 param_2,ulong param_3)

{
  long lVar1;
  undefined8 uVar2;

  uVar2 = 0xe00002db;
  if (param_3 < 0x26) {
    lVar1 = *(long *)(param_1 + 0x10);
    uVar2 = 0;
    *(undefined8 *)(lVar1 + 0x3cd) = 0;
    *(undefined8 *)(lVar1 + 0x3c8) = 0;
    *(undefined8 *)(lVar1 + 0x3c0) = 0;
    *(undefined8 *)(lVar1 + 0x3b8) = 0;
    *(undefined8 *)(lVar1 + 0x3b0) = 0;
    if ((param_3 != 0) && (param_3 != 0x25)) {
      lVar1 = *(long *)(param_1 + 0x10);
      *(ulong *)(lVar1 + 0x3d8) = param_3;
      FUN_ffffff8000101080(lVar1 + 0x3b0);
    }
  }
  return uVar2;
}



/* IO80211BssManager::isLikelyOrbiMeshNetwork() @ ffffff800226733c */

undefined8 IO80211BssManager__isLikelyOrbiMeshNetwork__(void)

{
  return 0;
}



/* MISSING @ 0xffffff8002267abc */

/* FUN_ffffff8002267afa @ ffffff8002267afa */

undefined8 FUN_ffffff8002267afa(long param_1,undefined8 param_2,ulong param_3)

{
  char cVar1;
  ulong uVar2;
  ulong uVar3;
  long lVar4;
  undefined8 uVar5;
  char *pcVar6;

  uVar5 = 0xe00002db;
  if (param_3 < 0x102) {
    lVar4 = *(long *)(param_1 + 0x10) + 600;
    if (param_3 == 0) {
                    /* WARNING: Subroutine does not return */
      FUN_ffffff8000101100(lVar4,0x101);
    }
    FUN_ffffff8000101080(lVar4,param_2,param_3);
    lVar4 = *(long *)(param_1 + 0x10);
    *(ulong *)(lVar4 + 0x250) = param_3;
    if (*(char *)(lVar4 + 600) == '\0') {
      uVar2 = 0;
      do {
        uVar3 = param_3;
        if (param_3 - 1 == uVar2) break;
        uVar3 = uVar2 + 1;
        pcVar6 = (char *)(lVar4 + 0x259 + uVar2);
        uVar2 = uVar3;
      } while (*pcVar6 == '\0');
      pcVar6 = "all zeros";
      if (uVar3 < param_3) {
        pcVar6 = "nonzero";
      }
    }
    else {
      pcVar6 = "nonzero";
    }
    uVar5 = 0;
    if ((*(long *)(lVar4 + 0x440) != 0) &&
       (cVar1 = FUN_ffffff8003222762(*(long *)(lVar4 + 0x440),0,0x800), cVar1 != '\0')) {
      uVar5 = 0;
      FUN_ffffff800322096e
                (*(undefined8 *)(*(long *)(param_1 + 0x10) + 0x440),0x800,
                 "[ik] %s@%d: Setting WPA/RSN IE has len %d, is %s\n","setAssocRSNIE",0x464,
                 *(undefined4 *)(*(long *)(param_1 + 0x10) + 0x250),pcVar6);
    }
  }
  return uVar5;
}



/* FUN_ffffff8002268000 @ ffffff8002268000 */

undefined8 FUN_ffffff8002268000(long param_1)

{
  undefined8 uVar1;

  if ((long *)**(long **)(param_1 + 0x10) != (long *)0x0) {
                    /* WARNING: Could not recover jumptable at 0xffffff8002268014. Too many branches
                        */
                    /* WARNING: Treating indirect jump as call */
    uVar1 = (**(code **)(*(long *)**(long **)(param_1 + 0x10) + 0x2b0))();
    return uVar1;
  }
  return 0;
}



/* FUN_ffffff800226801e @ ffffff800226801e */

ulong FUN_ffffff800226801e(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x439)) & 0xffffffffffffff01;
}



/* IO80211BssManager::setAdHocCreated(bool) @ ffffff8002268030 */

void IO80211BssManager__setAdHocCreated_bool_(long param_1,undefined1 param_2)

{
  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x439) = param_2;
  return;
}



/* FUN_ffffff8002268042 @ ffffff8002268042 */

ulong FUN_ffffff8002268042(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43e)) & 0xffffffffffffff01;
}



/* IO80211BssManager::setSISOAssoc(bool) @ ffffff8002268054 */

void IO80211BssManager__setSISOAssoc_bool_(long param_1,undefined1 param_2)

{
  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43e) = param_2;
  return;
}



/* IO80211BssManager::setPrivateMacJoinStatus(bool) @ ffffff8002268066 */

void IO80211BssManager__setPrivateMacJoinStatus_bool_(long param_1,undefined1 param_2)

{
  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43a) = param_2;
  return;
}



/* IO80211BssManager::setDeviceTypeInDhcpAllowStatus(bool) @ ffffff8002268078 */

void IO80211BssManager__setDeviceTypeInDhcpAllowStatus_bool_(long param_1,undefined1 param_2)

{
  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43b) = param_2;
  return;
}



/* IO80211BssManager::getPrivateMacJoinStatus() @ ffffff800226808a */

ulong IO80211BssManager__getPrivateMacJoinStatus__(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43a)) & 0xffffffffffffff01;
}



/* IO80211BssManager::getDeviceTypeInDhcpAllowStatus() @ ffffff800226809c */

ulong IO80211BssManager__getDeviceTypeInDhcpAllowStatus__(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43b)) & 0xffffffffffffff01;
}



/* IO80211BssManager::setAssociateToHotspotInWoWMode(bool) @ ffffff80022680ae */

void IO80211BssManager__setAssociateToHotspotInWoWMode_bool_(long param_1,undefined1 param_2)

{
  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43c) = param_2;
  return;
}



/* IO80211BssManager::isAssociateToHotspotInWoWMode() @ ffffff80022680c0 */

ulong IO80211BssManager__isAssociateToHotspotInWoWMode__(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43c)) & 0xffffffffffffff01;
}



/* MISSING @ 0xffffff80022680e0 */

/* IO80211BssManager::get6gStandAloneTopology() @ ffffff8002268106 */

ulong IO80211BssManager__get6gStandAloneTopology__(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(undefined1 *)(*(long *)(param_1 + 0x10) + 0x43d)) & 0xffffffffffffff01;
}



/* IO80211BssManager::set6gStandAloneTopology(bool) @ ffffff8002268118 */

void IO80211BssManager__set6gStandAloneTopology_bool_(long param_1,undefined1 param_2)

{
  long lVar1;

  lVar1 = *(long *)(param_1 + 0x10);
  *(undefined1 *)(lVar1 + 0x43d) = param_2;
  lVar1 = *(long *)(lVar1 + 0x440);
  if (lVar1 != 0) {
                    /* WARNING: Subroutine does not return */
    FUN_ffffff8003222728(lVar1,0);
  }
  return;
}



/* IO80211BssManager::isMloConnection() @ ffffff8002268646 */

undefined8 IO80211BssManager__isMloConnection__(long param_1)

{
  return CONCAT71((int7)((ulong)*(long *)(param_1 + 0x10) >> 8),
                  *(char *)(*(long *)(param_1 + 0x10) + 0x13e) != '\0');
}
