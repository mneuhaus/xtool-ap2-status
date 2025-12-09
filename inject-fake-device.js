/**
 * xTool Studio Fake Device Injection Script
 *
 * Run this in xTool Studio's DevTools console (View > Toggle Developer Tools)
 * to inject a fake device entry and unlock more functionality.
 *
 * Usage:
 * 1. Open xTool Studio
 * 2. Press Cmd+Option+I (Mac) or Ctrl+Shift+I (Windows) to open DevTools
 * 3. Go to Console tab
 * 4. Paste this entire script and press Enter
 * 5. Restart xTool Studio or refresh the editor
 */

(function() {
  'use strict';

  // Available devices from ext.json - pick one to inject
  const DEVICES = {
    M1: {
      name: "M1",
      versionId: "18",
      deviceCode: "MLM",
      machineType: "LASER",
      priority: 3,
      hasMultiPower: true,
      workspaceId: "xcs-ext-m1",
      extId: "M1",
      contentId: "xTool-m1-ext"
    },
    S1: {
      name: "S1",
      versionId: "32",
      alias: ["D2"],
      deviceCode: "MD2",
      machineType: "LASER",
      priority: 5,
      hasMultiPower: true,
      workspaceId: "xcs-ext-d2",
      extId: "S1",
      contentId: "xTool-s1-ext"
    },
    D1Pro: {
      name: "D1 Pro",
      versionId: "31",
      deviceCode: "MDP",
      machineType: "LASER",
      priority: 1,
      hasMultiPower: true,
      workspaceId: "xcs-ext-d1-pro",
      extId: "D1Pro",
      contentId: "xTool-d1pro-ext"
    },
    F1: {
      name: "F1",
      versionId: "51",
      deviceCode: "MF1",
      machineType: "LASER",
      priority: 10,
      hasMultiPower: false,
      workspaceId: "xcs-ext-f1",
      extId: "F1",
      contentId: "xTool-f1-ext"
    },
    P2: {
      name: "P2",
      versionId: "21",
      deviceCode: "MP2",
      machineType: "LASER",
      priority: 7,
      hasMultiPower: false,
      workspaceId: "xcs-ext-p2",
      extId: "P2",
      contentId: "xTool-p2-ext"
    }
  };

  // Select which device to inject (change this to try different devices)
  const SELECTED_DEVICE = 'M1';
  const device = DEVICES[SELECTED_DEVICE];

  console.log(`[Fake Device Injector] Injecting ${device.name} (${device.deviceCode})...`);

  // Create the fake device info object
  const fakeDeviceInfo = {
    from: "initN",
    id: Math.floor(Math.random() * 1000) + 1,
    deviceCode: device.deviceCode,
    saleName: device.name,
    category: "laser",
    brand: "xTool",
    model: device.name,
    extId: device.extId,
    extName: device.name,
    workspaceId: device.workspaceId,
    contentId: device.contentId,
    machineType: device.machineType,
    hasMultiPower: device.hasMultiPower,
    versionId: device.versionId,
    priority: device.priority,
    connected: false,
    createdAt: Date.now()
  };

  // Key localStorage items used by xTool Studio
  const storageKeys = {
    newProjectDefaultExtInfo: 'newProjectDefaultExtInfo',
    materialDeviceBasicInfo: 'material-device-basic-info',
    selectDevice: 'SELECT_DEVICE',
    diagnosticsDeviceList: 'DiagnosticsDeviceList',
    defaultDeviceModes: 'DEFAULT_DEVICE_MODES',
    extList: 'extList'
  };

  // Inject into various localStorage keys
  try {
    // 1. Set as default project device
    localStorage.setItem(storageKeys.newProjectDefaultExtInfo, JSON.stringify(fakeDeviceInfo));
    console.log('[Fake Device Injector] Set newProjectDefaultExtInfo');

    // 2. Set material device basic info
    const materialInfo = {
      deviceCode: device.deviceCode,
      extId: device.extId,
      name: device.name
    };
    localStorage.setItem(storageKeys.materialDeviceBasicInfo, JSON.stringify(materialInfo));
    console.log('[Fake Device Injector] Set material-device-basic-info');

    // 3. Set selected device
    localStorage.setItem(storageKeys.selectDevice, device.deviceCode);
    console.log('[Fake Device Injector] Set SELECT_DEVICE');

    // 4. Add to diagnostics device list
    const existingDiagList = JSON.parse(localStorage.getItem(storageKeys.diagnosticsDeviceList) || '[]');
    if (!existingDiagList.find(d => d.deviceCode === device.deviceCode)) {
      existingDiagList.push({
        deviceCode: device.deviceCode,
        name: device.name,
        extId: device.extId,
        addedAt: Date.now()
      });
      localStorage.setItem(storageKeys.diagnosticsDeviceList, JSON.stringify(existingDiagList));
      console.log('[Fake Device Injector] Added to DiagnosticsDeviceList');
    }

    // 5. Set default device modes
    const deviceModes = {
      [device.deviceCode]: {
        mode: 'normal',
        configured: true
      }
    };
    localStorage.setItem(storageKeys.defaultDeviceModes, JSON.stringify(deviceModes));
    console.log('[Fake Device Injector] Set DEFAULT_DEVICE_MODES');

    // 6. Try to store in extList if needed
    const existingExtList = JSON.parse(localStorage.getItem(storageKeys.extList) || '[]');
    if (!existingExtList.find(e => e.extId === device.extId)) {
      existingExtList.push({
        extId: device.extId,
        name: device.name,
        deviceCode: device.deviceCode,
        contentId: device.contentId
      });
      localStorage.setItem(storageKeys.extList, JSON.stringify(existingExtList));
      console.log('[Fake Device Injector] Added to extList');
    }

    console.log('');
    console.log('==========================================');
    console.log('[Fake Device Injector] SUCCESS!');
    console.log(`Injected: ${device.name} (${device.deviceCode})`);
    console.log('==========================================');
    console.log('');
    console.log('Next steps:');
    console.log('1. Close this DevTools window');
    console.log('2. Restart xTool Studio OR');
    console.log('3. Click File > New to create a new project');
    console.log('');
    console.log('The fake device should now appear as the default device.');
    console.log('Note: Device will show as "disconnected" since it\'s not real.');
    console.log('');

    // Return summary for debugging
    return {
      success: true,
      device: fakeDeviceInfo,
      keysSet: Object.keys(storageKeys)
    };

  } catch (error) {
    console.error('[Fake Device Injector] Error:', error);
    return {
      success: false,
      error: error.message
    };
  }
})();
