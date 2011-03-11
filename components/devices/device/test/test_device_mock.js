/* vim: set sw=2 :miv */
/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2011 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */


/**
 * \brief Device tests - Mock device
 */

function runTest () {
  var device = Components.classes["@songbirdnest.com/Songbird/Device/DeviceTester/MockDevice;1"]
                         .createInstance(Components.interfaces.sbIDevice);
  assertEqual(device.name, "Bob's Mock Device");
  assertEqual(device.productName, "Mock Device");
  
  assertEqual("" + device.id, "" + device.id, "device ID not equal");
  assertEqual("" + device.controllerId, "" + device.controllerId, "controller ID not equal");
  
  assertFalse(device.connected);
  
  device.connect();
  assertTrue(device.connected);
  try {
    device.connect();
    fail("Re-connected device");
  } catch(e){
    /* expected to throw */
  }
  assertTrue(device.connected);

  device.QueryInterface(Components.interfaces.sbIDeviceEventTarget);
  var wasFired = false;
  var handler = function handler(event) { 
    if (event.type == Ci.sbIDeviceEvent.EVENT_DEVICE_REMOVED) {
      log("Device removed event fired");
      try {
        // Device was disconnected, continue on testing
        assertFalse(device.connected);

        assertFalse(device.threaded);

        log("Reconnecting");
        device.connect();

        test_prefs(device);

        /* TODO: device.capabilities */

        /* TODO: device.content */

        /* TODO: device.parameters */

        test_properties(device);

        test_event(device);

        function continueTest()
        {
          if (!device.connected)
            device.connect();
          try {
            test_library(device);

            test_sync_settings(device);
          }
          finally {
            // stop a circular reference
            if (device.connected)
              device.disconnect();
          }
          testFinished();
        }
        test_request(device, continueTest);
        return;
      }
      catch (e) {
        log("Exception occurred: " + e);
        fail();
      }    
    }
  }
  device.addEventListener(handler);

  device.disconnect();
  try {
    device.disconnect();
  } catch(e) {
    fail("Re-disconnected device should not fail");
  }
  testPending();
}

function test_prefs(device) {
  assertTrue(typeof(device.getPreference("hello")) == "undefined");
  
  device.setPreference("world", 3);
  assertEqual(device.getPreference("world"), 3);
  assertEqual(typeof(device.getPreference("world")), "number");
  device.setPreference("world", "goat");
  assertEqual(device.getPreference("world"), "goat");
  assertEqual(typeof(device.getPreference("world")), "string");
  device.setPreference("world", true);
  assertEqual(device.getPreference("world"), true);
  assertEqual(typeof(device.getPreference("world")), "boolean");
  
  with (Components.interfaces.sbIDevice) {
    device.setPreference("state", 0);
    assertEqual(device.state, STATE_IDLE);
    device.setPreference("state", 1);
    assertEqual(device.state, STATE_SYNCING);
    device.setPreference("state", 2);
    assertEqual(device.state, STATE_COPYING);
    device.setPreference("state", 3);
    assertEqual(device.state, STATE_DELETING);
    device.setPreference("state", 4);
    assertEqual(device.state, STATE_UPDATING);
    device.setPreference("state", 5);
    assertEqual(device.state, STATE_MOUNTING);
    device.setPreference("state", 6);
    assertEqual(device.state, STATE_DOWNLOADING);
    device.setPreference("state", 7);
    assertEqual(device.state, STATE_UPLOADING);
    device.setPreference("state", 8);
    assertEqual(device.state, STATE_DOWNLOAD_PAUSED);
    device.setPreference("state", 9);
    assertEqual(device.state, STATE_UPLOAD_PAUSED);
    device.setPreference("state", 10);
    assertEqual(device.state, STATE_DISCONNECTED);
  }
}

function test_event(device) {
  /* test as a event target */
  // I didn't bother with CI on the mock device
  device.QueryInterface(Components.interfaces.sbIDeviceEventTarget);
  var wasFired = false;
  var handler = function handler() { wasFired = true; }
  device.addEventListener(handler);
  var event = Components.classes["@songbirdnest.com/Songbird/DeviceManager;2"]
                        .getService(Components.interfaces.sbIDeviceManager2)
                        .createEvent(0);
  device.dispatchEvent(event);
  assertTrue(wasFired, "event handler not called");
  
  device.removeEventListener(handler);
}

function test_request(device, continueAction) {
  /* test as sbIMockDevice (request push/pop) */
  device.QueryInterface(Ci.sbIMockDevice);
  
  // test the properties
  var item = { QueryInterface:function(){return this} };
  item.wrappedJSObject = item;
  var list = { QueryInterface:function(){return this} };
  list.wrappedJSObject = list;
  var data = { /* nothing needed */ };
  data.wrappedJSObject = data;
  var params = { item: item,
                 list: list,
                 data: data,
                 index: 999,
                 otherIndex: 1024 };
  device.submitRequest(0x01dbeef, createPropertyBag(params));
  // Wait for a request to come in using a timeout
  function requestCheck() {
    request = device.popRequest();
    if (request) {
      checkPropertyBag(request, params);
      log("item transfer ID: " + request.getProperty("itemTransferID"));
      assertTrue(request.getProperty("itemTransferID") > 3,
                   "Obviously bad item transfer ID");

      request = null; /* unleak */
      continueAction();
    }
    else {
      doTimeout(100, requestCheck);
    }
    return;
  }
  doTimeout(100, requestCheck);
}

function test_library(device) {
  assertEqual(device,
              device.content
                    .libraries
                    .queryElementAt(0, Ci.sbIDeviceLibrary)
                    .device);
  assertEqual(device.defaultLibrary,
              device.content
                    .libraries
                    .queryElementAt(0, Ci.sbIDeviceLibrary));
}

function test_sync_settings(device) {
  log("Testing initial mode");
  let syncSettings = device.defaultLibrary.syncSettings;

  log("Changing management type to all");
  syncSettings = device.defaultLibrary.tempSyncSettings;
  
  let audioSyncSettings = syncSettings.getMediaSettings(
                                           Ci.sbIDeviceLibrary.MEDIATYPE_AUDIO);
  let videoSyncSettings = syncSettings.getMediaSettings(
                                           Ci.sbIDeviceLibrary.MEDIATYPE_VIDEO);
  let imageSyncSettings = syncSettings.getMediaSettings(
                                           Ci.sbIDeviceLibrary.MEDIATYPE_IMAGE);
  audioSyncSettings.mgmtType = 
                             Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_ALL;                             
  assertEqual(audioSyncSettings.mgmtType, 
              Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_ALL);
  videoSyncSettings.mgmtType = 
                             Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_ALL;
  assertEqual(videoSyncSettings.mgmtType, 
              Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_ALL);
  imageSyncSettings.mgmtType = 
                            Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_NONE;
  assertEqual(imageSyncSettings.mgmtType, 
              Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_NONE);
  log("Applying changes");
  device.defaultLibrary.applySyncSettings();
  syncSettings = device.defaultLibrary.syncSettings;

  log("Checking audio management setting");              
  assertEqual(syncSettings.getMediaSettings(
                                  Ci.sbIDeviceLibrary.MEDIATYPE_AUDIO).mgmtType,
              Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_ALL);
  log("Checking video management setting");              
  assertEqual(syncSettings.getMediaSettings(
                                  Ci.sbIDeviceLibrary.MEDIATYPE_VIDEO).mgmtType,
              Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_ALL);
  log("Checking image management setting");              
  assertEqual(syncSettings.getMediaSettings(
                                  Ci.sbIDeviceLibrary.MEDIATYPE_IMAGE).mgmtType,
              Ci.sbIDeviceLibraryMediaSyncSettings.SYNC_MGMT_NONE);
}

function test_properties(device) {
  var properties = device.properties;
  
  log("friendlyName: " + properties.friendlyName);
  assertEqual(properties.friendlyName, "Testing Device");
  log("serialNumber: " + properties.serialNumber);
  assertEqual(properties.serialNumber, "ACME-9000-0001-2000-3000");
  log("modelNumber: " + properties.modelNumber);
  assertEqual(properties.modelNumber, "ACME 9000");
  log("vendorName: " + properties.vendorName);
  assertEqual(properties.vendorName, "ACME Inc.");
  log("firmwareVersion: " + properties.firmwareVersion);
  assertEqual(properties.firmwareVersion, "1.0.0.0");
  
  properties.friendlyName = "New Friendly Test Device";
  assertEqual(properties.friendlyName, "New Friendly Test Device");
}

function createPropertyBag(aParams) {
  var bag = Cc["@mozilla.org/hash-property-bag;1"]
              .createInstance(Ci.nsIWritablePropertyBag);
  for (var name in aParams) {
    bag.setProperty(name, aParams[name]);
  }
  return bag;
}

function checkPropertyBag(aBag, aParams) {
  for (var name in  aParams) {
    try {
      var val = aBag.getProperty(name);
    } catch (e) {
      log('Failed to get property "' + name + '"');
      throw(e);
    }
    assertTrue(val, 'Cannot find property "' + name + '"');
    if (typeof(aParams[name]) == "object" && "wrappedJSObject" in aParams[name])
      val = val.wrappedJSObject;
    assertEqual(aParams[name],
                val,
                'property "' + name + '" not equal');
    log('"' + name + '" is ' + val);
  }
}
