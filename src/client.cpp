/*
 *  pvr.python - A PVR client for Kodi using Python
 *  Copyright © 2016 RunasSudo (Yingtong Li)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <Python.h>

#include "client.h"
#include "xbmc_pvr_dll.h"
#include <p8-platform/util/util.h>

using namespace std;
using namespace ADDON;

CHelper_libXBMC_addon *XBMC = NULL;
CHelper_libXBMC_pvr *PVR = NULL;

PyThreadState* pyState;
PyObject* pvrImpl;
void* streamHandle;

ADDON_HANDLE addon_handle;

extern "C" {

#define MAYBE_LOG_CALL() XBMC->Log(LOG_DEBUG, "%s - Called", __FUNCTION__);
#define MAYBE_LOG_NYI() XBMC->Log(LOG_DEBUG, "%s - NYI", __FUNCTION__);

#define PYTHON_LOCK() PyEval_AcquireLock(); PyThreadState_Swap(pyState);
#define PYTHON_UNLOCK() PyThreadState_Swap(NULL); PyEval_ReleaseLock();

// BEGIN PYTHON<->C HELPER FUNCTIONS

long PyInt_AsLong_DR(PyObject* obj) {
	long val = PyInt_AsLong(obj);
	Py_DECREF(obj);
	return val;
}

bool PyBool_AsBool(PyObject* obj) {
	return (obj == Py_True);
}

bool PyBool_AsBool_DR(PyObject* obj) {
	bool val = (obj == Py_True);
	Py_DECREF(obj);
	return val;
}

char* PyString_SafeAsString(PyObject* obj) {
	char* cString;
	if (PyUnicode_Check(obj)) {
		// Encode the Unicode as ASCII if necessary
		PyObject* pyString = PyUnicode_AsEncodedString(obj, "ascii", "ignore");
		cString = PyString_AsString(pyString);
		Py_DECREF(pyString);
	} else {
		cString = PyString_AsString(obj);
	}
	
	// Copy the string
	char* cString2 = (char*) malloc(strlen(cString) + 1); // +1 for the NUL
	strcpy(cString2, cString);
	
	return cString2;
}

char* PyString_SafeAsString_DR(PyObject* obj) {
	char* val = PyString_SafeAsString(obj);
	Py_DECREF(obj);
	return val;
}

// You must Py_DECREF the return value once you're done!
PyObject* pyCall(PyObject* obj, const char* func, PyObject* args) {
	PyObject* pyFunc = PyObject_GetAttrString(obj, func);
	PyObject* pyArgs = args;
	if (args == NULL) {
		pyArgs = PyTuple_New(0);
	}
	PyObject* pyReturnValue = PyObject_CallObject(pyFunc, pyArgs);
	if (PyErr_Occurred() != NULL) { PyErr_Print(); PyErr_Clear(); Py_INCREF(Py_None); PYTHON_UNLOCK(); return Py_None; }
	if (args == NULL) {
		Py_DECREF(pyArgs);
	}
	Py_DECREF(pyFunc);
	
	return pyReturnValue;
}

// You must Py_DECREF the return value once you're done!
PyObject* pyLockCall(PyObject* obj, const char* func, PyObject* args) {
	PYTHON_LOCK();
	PyObject* pyReturnValue = pyCall(obj, func, args);
	PYTHON_UNLOCK();
	return pyReturnValue;
}

char* pyCallString(PyObject* obj, const char* func, PyObject* args) {
	PyObject* pyReturnValue = pyCall(obj, func, args);
	char* returnValue = PyString_SafeAsString(pyReturnValue);
	Py_DECREF(pyReturnValue);
	return returnValue;
}

char* pyLockCallString(PyObject* obj, const char* func, PyObject* args) {
	PYTHON_LOCK();
	char* returnValue = pyCallString(obj, func, args);
	PYTHON_UNLOCK();
	return returnValue;
}

int pyCallInt(PyObject* obj, const char* func, PyObject* args) {
	PyObject* pyReturnValue = pyCall(obj, func, args);
	int returnValue = PyInt_AsLong(pyReturnValue);
	Py_DECREF(pyReturnValue);
	return returnValue;
}

int pyLockCallInt(PyObject* obj, const char* func, PyObject* args) {
	PYTHON_LOCK();
	int returnValue = pyCallInt(obj, func, args);
	PYTHON_UNLOCK();
	return returnValue;
}

PVR_ERROR pyLockCallPVRError(PyObject* obj, const char* func, PyObject* args) {
	PYTHON_LOCK();
	int returnValue = pyCallInt(obj, func, args);
	PYTHON_UNLOCK();
	return ((PVR_ERROR) returnValue);
}

bool pyCallBool(PyObject* obj, const char* func, PyObject* args) {
	PyObject* pyReturnValue = pyCall(obj, func, args);
	bool returnValue = PyBool_AsBool(pyReturnValue);
	Py_DECREF(pyReturnValue);
	return returnValue;
}

bool pyLockCallBool(PyObject* obj, const char* func, PyObject* args) {
	PYTHON_LOCK();
	bool returnValue = pyCallBool(obj, func, args);
	PYTHON_UNLOCK();
	return returnValue;
}

// BEGIN C->PYTHON BRIDGE FUNCTIONS

static PyObject* bridge_XBMC_Log(PyObject* self, PyObject* args)
{
	const char *s;
	if (!PyArg_ParseTuple(args, "s", &s)) {
		PyErr_SetString(PyExc_TypeError, "parameter must be a string");
		return NULL;
	}
	
	XBMC->Log(LOG_DEBUG, "%s - %s", __FUNCTION__, s);
	
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* bridge_PVR_TransferChannelEntry(PyObject* self, PyObject* args)
{
	PyObject* pyChannel = PyTuple_GetItem(args, 0);
	
	PVR_CHANNEL xbmcChannel;
	memset(&xbmcChannel, 0, sizeof(PVR_CHANNEL));
	
	xbmcChannel.iUniqueId = PyInt_AsLong_DR(PyObject_GetAttrString(pyChannel, "uniqueId"));
	xbmcChannel.bIsRadio = PyBool_AsBool_DR(PyObject_GetAttrString(pyChannel, "isRadio"));
	xbmcChannel.iChannelNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyChannel, "channelNumber"));
	xbmcChannel.iSubChannelNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyChannel, "subChannelNumber"));
	strcpy(xbmcChannel.strChannelName, PyString_SafeAsString_DR(PyObject_GetAttrString(pyChannel, "channelName")));
	strcpy(xbmcChannel.strInputFormat, PyString_SafeAsString_DR(PyObject_GetAttrString(pyChannel, "inputFormat")));
	strcpy(xbmcChannel.strStreamURL, PyString_SafeAsString_DR(PyObject_GetAttrString(pyChannel, "streamURL")));
	xbmcChannel.iEncryptionSystem = PyInt_AsLong_DR(PyObject_GetAttrString(pyChannel, "encryptionSystem"));
	strcpy(xbmcChannel.strIconPath, PyString_SafeAsString_DR(PyObject_GetAttrString(pyChannel, "iconPath")));
	xbmcChannel.bIsHidden = PyBool_AsBool_DR(PyObject_GetAttrString(pyChannel, "isHidden"));
	
	PVR->TransferChannelEntry(addon_handle, &xbmcChannel);
	
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* bridge_PVR_TransferChannelGroup(PyObject* self, PyObject* args)
{
	PyObject* pyGroup = PyTuple_GetItem(args, 0);
	
	PVR_CHANNEL_GROUP xbmcGroup;
	memset(&xbmcGroup, 0, sizeof(PVR_CHANNEL_GROUP));
	
	strcpy(xbmcGroup.strGroupName, PyString_SafeAsString_DR(PyObject_GetAttrString(pyGroup, "groupName")));
	xbmcGroup.bIsRadio = PyBool_AsBool_DR(PyObject_GetAttrString(pyGroup, "isRadio"));
	xbmcGroup.iPosition = PyInt_AsLong_DR(PyObject_GetAttrString(pyGroup, "position"));
	
	PVR->TransferChannelGroup(addon_handle, &xbmcGroup);
	
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* bridge_PVR_TransferChannelGroupMember(PyObject* self, PyObject* args)
{
	PyObject* pyGroupMember = PyTuple_GetItem(args, 0);
	
	PVR_CHANNEL_GROUP_MEMBER xbmcGroupMember;
	memset(&xbmcGroupMember, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));
	
	strcpy(xbmcGroupMember.strGroupName, PyString_SafeAsString_DR(PyObject_GetAttrString(pyGroupMember, "groupName")));
	xbmcGroupMember.iChannelUniqueId = PyInt_AsLong_DR(PyObject_GetAttrString(pyGroupMember, "channelUniqueId"));
	xbmcGroupMember.iChannelNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyGroupMember, "channelNumber"));
	
	PVR->TransferChannelGroupMember(addon_handle, &xbmcGroupMember);
	
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* bridge_PVR_TransferTimerEntry(PyObject* self, PyObject* args)
{
	PyObject* pyEntry = PyTuple_GetItem(args, 0);
	
	PVR_TIMER xbmcEntry;
	memset(&xbmcEntry, 0, sizeof(PVR_TIMER));
	
	xbmcEntry.iClientIndex = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "clientIndex"));
	xbmcEntry.iParentClientIndex = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "parentClientIndex"));
	xbmcEntry.iClientChannelUid = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "clientChannelUid"));
	xbmcEntry.startTime = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "_cstartTime"));
	xbmcEntry.endTime = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "_cendTime"));
	xbmcEntry.bStartAnyTime = PyBool_AsBool_DR(PyObject_GetAttrString(pyEntry, "startAnyTime"));
	xbmcEntry.bEndAnyTime = PyBool_AsBool_DR(PyObject_GetAttrString(pyEntry, "endAnyTime"));
	xbmcEntry.state = (PVR_TIMER_STATE) PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "state"));
	xbmcEntry.iTimerType = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "timerType"));
	strcpy(xbmcEntry.strTitle, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "title")));
	strcpy(xbmcEntry.strEpgSearchString, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "epgSearchString")));
	xbmcEntry.bFullTextEpgSearch = PyBool_AsBool_DR(PyObject_GetAttrString(pyEntry, "fullTextEpgSearch"));
	strcpy(xbmcEntry.strDirectory, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "directory")));
	strcpy(xbmcEntry.strSummary, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "summary")));
	xbmcEntry.iPriority = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "priority"));
	xbmcEntry.iLifetime = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "lifetime"));
	xbmcEntry.iMaxRecordings = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "maxRecordings"));
	xbmcEntry.iRecordingGroup = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "recordingGroup"));
	xbmcEntry.firstDay = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "_cfirstDay"));
	xbmcEntry.iWeekdays = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "weekdays"));
	xbmcEntry.iPreventDuplicateEpisodes = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "preventDuplicateEpisodes"));
	xbmcEntry.iEpgUid = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "epgUid"));
	xbmcEntry.iMarginStart = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "marginStart"));
	xbmcEntry.iMarginEnd = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "marginEnd"));
	xbmcEntry.iGenreType = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "genreType"));
	xbmcEntry.iGenreSubType = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "genreSubType"));
	
	PVR->TransferTimerEntry(addon_handle, &xbmcEntry);
	
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* bridge_PVR_TransferRecordingEntry(PyObject* self, PyObject* args)
{
	PyObject* pyEntry = PyTuple_GetItem(args, 0);
	
	PVR_RECORDING xbmcEntry;
	memset(&xbmcEntry, 0, sizeof(PVR_RECORDING));
	
	strcpy(xbmcEntry.strRecordingId, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "recordingId")));
	strcpy(xbmcEntry.strTitle, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "title")));
	strcpy(xbmcEntry.strEpisodeName, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "episodeName")));
	xbmcEntry.iSeriesNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "seriesNumber"));
	xbmcEntry.iEpisodeNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "episodeNumber"));
	xbmcEntry.iYear = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "year"));
	strcpy(xbmcEntry.strStreamURL, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "streamURL")));
	strcpy(xbmcEntry.strDirectory, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "directory")));
	strcpy(xbmcEntry.strPlotOutline, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "plotOutline")));
	strcpy(xbmcEntry.strPlot, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "plot")));
	strcpy(xbmcEntry.strChannelName, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "channelName")));
	strcpy(xbmcEntry.strIconPath, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "iconPath")));
	strcpy(xbmcEntry.strThumbnailPath, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "thumbnailPath")));
	strcpy(xbmcEntry.strFanartPath, PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "fanartPath")));
	xbmcEntry.recordingTime = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "_crecordingTime"));
	xbmcEntry.iDuration = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "duration"));
	xbmcEntry.iPriority = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "priority"));
	xbmcEntry.iLifetime = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "lifetime"));
	xbmcEntry.iGenreType = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "genreType"));
	xbmcEntry.iGenreSubType = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "genreSubType"));
	xbmcEntry.iPlayCount = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "playCount"));
	xbmcEntry.iLastPlayedPosition = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "lastPlayedPosition"));
	xbmcEntry.bIsDeleted = PyBool_AsBool_DR(PyObject_GetAttrString(pyEntry, "isDeleted"));
	xbmcEntry.iEpgEventId = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "epgEventId"));
	xbmcEntry.iChannelUid = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "channelUid"));
	xbmcEntry.iChannelUid = PVR_CHANNEL_INVALID_UID;
	xbmcEntry.channelType = (PVR_RECORDING_CHANNEL_TYPE) PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "channelType"));
	
	PVR->TransferRecordingEntry(addon_handle, &xbmcEntry);
	
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* bridge_PVR_TransferEpgEntry(PyObject* self, PyObject* args)
{
	PyObject* pyEntry = PyTuple_GetItem(args, 0);
	
	EPG_TAG xbmcEntry;
	memset(&xbmcEntry, 0, sizeof(EPG_TAG));
	
	xbmcEntry.iUniqueBroadcastId = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "uniqueBroadcastId"));
	xbmcEntry.strTitle = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "title"));
	xbmcEntry.iChannelNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "channelNumber"));
	xbmcEntry.startTime = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "_cstartTime"));
	xbmcEntry.endTime = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "_cendTime"));
	xbmcEntry.strPlotOutline = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "plotOutline"));
	xbmcEntry.strPlot = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "plot"));
	xbmcEntry.strOriginalTitle = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "originalTitle"));
	xbmcEntry.strCast = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "cast"));
	xbmcEntry.strDirector = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "director"));
	xbmcEntry.strWriter = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "writer"));
	xbmcEntry.iYear = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "year"));
	xbmcEntry.strIMDBNumber = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "IMDBNumber"));
	xbmcEntry.strIconPath = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "iconPath"));
	xbmcEntry.iGenreType = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "genreType"));
	xbmcEntry.iGenreSubType = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "genreSubType"));
	xbmcEntry.strGenreDescription = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "genreDescription"));
	xbmcEntry.firstAired = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "_cfirstAired"));
	xbmcEntry.iParentalRating = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "parentalRating"));
	xbmcEntry.iStarRating = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "starRating"));
	xbmcEntry.bNotify = PyBool_AsBool_DR(PyObject_GetAttrString(pyEntry, "notify"));
	xbmcEntry.iSeriesNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "seriesNumber"));
	xbmcEntry.iEpisodeNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "episodeNumber"));
	xbmcEntry.iEpisodePartNumber = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "episodePartNumber"));
	xbmcEntry.strEpisodeName = PyString_SafeAsString_DR(PyObject_GetAttrString(pyEntry, "episodeName"));
	xbmcEntry.iFlags = PyInt_AsLong_DR(PyObject_GetAttrString(pyEntry, "flags"));
	
	PVR->TransferEpgEntry(addon_handle, &xbmcEntry);
	
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef bridgeMethods[] = {
	{"XBMC_Log", bridge_XBMC_Log, METH_VARARGS, ""},
	{"PVR_TransferChannelEntry", bridge_PVR_TransferChannelEntry, METH_VARARGS, ""},
	{"PVR_TransferChannelGroup", bridge_PVR_TransferChannelGroup, METH_VARARGS, ""},
	{"PVR_TransferChannelGroupMember", bridge_PVR_TransferChannelGroupMember, METH_VARARGS, ""},
	{"PVR_TransferTimerEntry", bridge_PVR_TransferTimerEntry, METH_VARARGS, ""},
	{"PVR_TransferRecordingEntry", bridge_PVR_TransferRecordingEntry, METH_VARARGS, ""},
	{"PVR_TransferEpgEntry", bridge_PVR_TransferEpgEntry, METH_VARARGS, ""},
	{NULL, NULL, 0, NULL}
};

// END PYTHON<->C FUNCTIONS

//void ADDON_ReadSettings(void)
//{
	//STUB
//}

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
	if (!hdl || !props)
		return ADDON_STATUS_UNKNOWN;
	
	PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;
	
	XBMC = new CHelper_libXBMC_addon;
	if (!XBMC->RegisterMe(hdl))
	{
		SAFE_DELETE(XBMC);
		return ADDON_STATUS_PERMANENT_FAILURE;
	}
	
	PVR = new CHelper_libXBMC_pvr;
	if (!PVR->RegisterMe(hdl))
	{
		SAFE_DELETE(PVR);
		SAFE_DELETE(XBMC);
		return ADDON_STATUS_PERMANENT_FAILURE;
	}
	
	XBMC->Log(LOG_DEBUG, "%s - Creating the PVR demo add-on", __FUNCTION__);
	
	PyEval_AcquireLock();
	pyState = Py_NewInterpreter();
	PyThreadState_Swap(pyState);
	
	Py_InitModule("bridge", bridgeMethods);
	
	// Setup the path
	PyObject* sysPath = PySys_GetObject((char*) "path");
	PyObject* pyClientPath = PyString_FromString(pvrprops->strClientPath);
	PyList_Append(sysPath, pyClientPath);
	Py_DECREF(pyClientPath);
	XBMC->Log(LOG_DEBUG, "%s - Added '%s' to sys.path", __FUNCTION__, pvrprops->strClientPath);
	
	// Import the module
	PyObject* pyName = PyString_FromString("pvrimpl");
	PyObject* pyModule = PyImport_Import(pyName);
	Py_DECREF(pyName);
	
	if (pyModule == NULL) {
		XBMC->Log(LOG_DEBUG, "%s - Failed to import Python PVR implementation module 'pvrimpl'", __FUNCTION__);
		SAFE_DELETE(PVR);
		SAFE_DELETE(XBMC);
		return ADDON_STATUS_PERMANENT_FAILURE;
	}
	
	XBMC->Log(LOG_DEBUG, "%s - Handing over to Python", __FUNCTION__);
	
	// Get an instance
	PyObject* pyFunc = PyObject_GetAttrString(pyModule, "getInstance");
	PyObject* pyArgs = PyTuple_New(0);
	pvrImpl = PyObject_CallObject(pyFunc, pyArgs);
	if (PyErr_Occurred() != NULL) { PyErr_Print(); PyErr_Clear(); PYTHON_UNLOCK(); return ADDON_STATUS_PERMANENT_FAILURE; }
	Py_DECREF(pyArgs);
	Py_DECREF(pyFunc);
	
	// Call the ADDON_Create function
	pyFunc = PyObject_GetAttrString(pvrImpl, "ADDON_Create");
	pyArgs = Py_BuildValue("({s:s, s:s, s:i})", "userPath", pvrprops->strUserPath, "clientPath", pvrprops->strClientPath, "epgMaxDays", pvrprops->iEpgMaxDays);
	PyObject* pyReturnValue = PyObject_CallObject(pyFunc, pyArgs);
	if (PyErr_Occurred() != NULL) { PyErr_Print(); PyErr_Clear(); PYTHON_UNLOCK(); return ADDON_STATUS_PERMANENT_FAILURE; }
	long returnValue = PyInt_AsLong(pyReturnValue);
	Py_DECREF(pyReturnValue);
	Py_DECREF(pyArgs);
	Py_DECREF(pyFunc);
	
	PyThreadState_Swap(NULL);
	PyEval_ReleaseLock();
	
	// Process the return value
	// Enums take on their integer indexes as value
	return ((ADDON_STATUS) returnValue);
}

ADDON_STATUS ADDON_GetStatus()
{
	MAYBE_LOG_NYI();
	return (ADDON_STATUS)6;
	
	//return m_CurStatus;
}

void ADDON_Destroy()
{
	MAYBE_LOG_NYI();
	PYTHON_LOCK();
	Py_EndInterpreter(pyState);
	PYTHON_UNLOCK();
	return;
	
	//delete m_data;
	//m_bCreated = false;
	//m_CurStatus = ADDON_STATUS_UNKNOWN;
}

bool ADDON_HasSettings()
{
	MAYBE_LOG_NYI();
	return true;
}

unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
	MAYBE_LOG_NYI();
	return 0;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
	MAYBE_LOG_NYI();
	return ADDON_STATUS_OK;
}

void ADDON_Stop()
{
	MAYBE_LOG_NYI();
}

void ADDON_FreeSettings()
{
	MAYBE_LOG_NYI();
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
	MAYBE_LOG_CALL();
	
	PYTHON_LOCK();
	
	PyObject* pyFunc = PyObject_GetAttrString(pvrImpl, "GetAddonCapabilities");
	PyObject* pyArgs = PyTuple_New(0);
	PyObject* pyReturnValue = PyObject_CallObject(pyFunc, pyArgs);
	if (PyErr_Occurred() != NULL) { PyErr_Print(); PyErr_Clear(); PYTHON_UNLOCK(); return PVR_ERROR_FAILED; }
	
	PyObject* pyCapabilities = PyTuple_GetItem(pyReturnValue, 1);
	pCapabilities->bSupportsEPG = (PyDict_GetItemString(pyCapabilities, "supportsEPG") == Py_True);
	pCapabilities->bSupportsTV = (PyDict_GetItemString(pyCapabilities, "supportsTV") == Py_True);
	pCapabilities->bSupportsRadio = (PyDict_GetItemString(pyCapabilities, "supportsRadio") == Py_True);
	pCapabilities->bSupportsRecordings = (PyDict_GetItemString(pyCapabilities, "supportsRecordings") == Py_True);
	pCapabilities->bSupportsRecordingsUndelete = (PyDict_GetItemString(pyCapabilities, "supportsRecordingsUndelete") == Py_True);
	pCapabilities->bSupportsTimers = (PyDict_GetItemString(pyCapabilities, "supportsTimers") == Py_True);
	pCapabilities->bSupportsChannelGroups = (PyDict_GetItemString(pyCapabilities, "supportsChannelGroups") == Py_True);
	pCapabilities->bSupportsChannelScan = (PyDict_GetItemString(pyCapabilities, "supportsChannelScan") == Py_True);
	pCapabilities->bSupportsChannelSettings = (PyDict_GetItemString(pyCapabilities, "supportsChannelSettings") == Py_True);
	pCapabilities->bHandlesInputStream = (PyDict_GetItemString(pyCapabilities, "handlesInputStream") == Py_True);
	pCapabilities->bHandlesDemuxing = (PyDict_GetItemString(pyCapabilities, "handlesDemuxing") == Py_True);
	pCapabilities->bSupportsRecordingPlayCount = (PyDict_GetItemString(pyCapabilities, "supportsRecordingPlayCount") == Py_True);
	pCapabilities->bSupportsLastPlayedPosition = (PyDict_GetItemString(pyCapabilities, "supportsLastPlayedPosition") == Py_True);
	pCapabilities->bSupportsRecordingEdl = (PyDict_GetItemString(pyCapabilities, "supportsRecordingEdl") == Py_True);
	
	PyObject* pyErrorCode = PyTuple_GetItem(pyReturnValue, 0);
	long errorCode = PyInt_AsLong(pyErrorCode);
	Py_DECREF(pyReturnValue);
	Py_DECREF(pyArgs);
	Py_DECREF(pyFunc);
	
	PYTHON_UNLOCK();
	
	return ((PVR_ERROR) errorCode);
}

const char *GetBackendName(void)
{
	MAYBE_LOG_CALL();
	return pyLockCallString(pvrImpl, "GetBackendName", NULL);
}

const char *GetConnectionString(void)
{
	MAYBE_LOG_CALL();
	return pyLockCallString(pvrImpl, "GetConnectionString", NULL);
}

const char *GetBackendVersion(void)
{
	MAYBE_LOG_CALL();
	return pyLockCallString(pvrImpl, "GetBackendVersion", NULL);
}

const char *GetBackendHostname(void)
{
	MAYBE_LOG_CALL();
	return pyLockCallString(pvrImpl, "GetBackendHostname", NULL);
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
	MAYBE_LOG_CALL();
	
	addon_handle = handle;
	return pyLockCallPVRError(pvrImpl, "_cGetChannels", Py_BuildValue("(b)", bRadio));
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
	MAYBE_LOG_CALL();
	
	addon_handle = handle;
	return pyLockCallPVRError(pvrImpl, "_cGetChannelGroups", Py_BuildValue("(b)", bRadio));
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
	MAYBE_LOG_CALL();
	
	addon_handle = handle;
	return pyLockCallPVRError(pvrImpl, "_cGetChannelGroupMembers", Py_BuildValue("(s)", group.strGroupName));
}

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
	MAYBE_LOG_NYI();
	
	/* TODO: Implement this to get support for the timer features introduced with PVR API 1.9.7 */
	return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
	MAYBE_LOG_CALL();
	
	addon_handle = handle;
	/* TODO: Change implementation to get support for the timer features introduced with PVR API 1.9.7 */
	return pyLockCallPVRError(pvrImpl, "_cGetTimers", NULL);
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
	MAYBE_LOG_CALL();
	
	addon_handle = handle;
	return pyLockCallPVRError(pvrImpl, "_cGetRecordings", Py_BuildValue("(b)", deleted));
}

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
	MAYBE_LOG_CALL();
	
	PYTHON_LOCK();
	
	PyObject* pyFunc = PyObject_GetAttrString(pvrImpl, "GetDriveSpace");
	PyObject* pyArgs = PyTuple_New(0);
	PyObject* pyReturnValue = PyObject_CallObject(pyFunc, pyArgs);
	if (PyErr_Occurred() != NULL) { PyErr_Print(); PyErr_Clear(); PYTHON_UNLOCK(); return PVR_ERROR_FAILED; }
	int errorCode = PyInt_AsLong(PyTuple_GetItem(pyReturnValue, 0));
	*iTotal = PyLong_AsLongLong(PyTuple_GetItem(pyReturnValue, 1));
	*iUsed = PyLong_AsLongLong(PyTuple_GetItem(pyReturnValue, 2));
	Py_DECREF(pyReturnValue);
	Py_DECREF(pyArgs);
	Py_DECREF(pyFunc);
	
	PYTHON_UNLOCK();
	
	return ((PVR_ERROR) errorCode);
}

int GetChannelsAmount(void)
{
	MAYBE_LOG_CALL();
	return pyLockCallInt(pvrImpl, "GetChannelsAmount", NULL);
}

int GetTimersAmount(void)
{
	MAYBE_LOG_CALL();
	return pyLockCallInt(pvrImpl, "GetTimersAmount", NULL);
}

int GetRecordingsAmount(bool deleted)
{
	MAYBE_LOG_CALL();
	return pyLockCallInt(pvrImpl, "GetRecordingsAmount", Py_BuildValue("(b)", deleted));
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
	MAYBE_LOG_CALL();
	
	addon_handle = handle;
	return pyLockCallPVRError(pvrImpl, "_cGetEPGForChannel", Py_BuildValue("(i, i, i)", channel.iUniqueId, iStart, iEnd));
}

void OnSystemSleep()
{
	MAYBE_LOG_NYI();
}

void OnSystemWake()
{
	MAYBE_LOG_NYI();
}

void OnPowerSavingActivated()
{
	MAYBE_LOG_NYI();
}

void OnPowerSavingDeactivated()
{
	MAYBE_LOG_NYI();
}

const char* GetPVRAPIVersion(void)
{
	static const char *strApiVersion = XBMC_PVR_API_VERSION;
	return strApiVersion;
}

const char* GetMininumPVRAPIVersion(void)
{
	static const char *strMinApiVersion = XBMC_PVR_MIN_API_VERSION;
	return strMinApiVersion;
}

const char* GetGUIAPIVersion(void)
{
	return ""; // GUI API not used
}

const char* GetMininumGUIAPIVersion(void)
{
	return ""; // GUI API not used
}

bool OpenLiveStream(const PVR_CHANNEL &channel)
{
	MAYBE_LOG_CALL();
	
	CloseLiveStream();
	
	PYTHON_LOCK();
	
	PyObject* pyFunc = PyObject_GetAttrString(pvrImpl, "OpenLiveStream");
	PyObject* pyArgs = Py_BuildValue("(i)", channel.iUniqueId);
	PyObject* pyReturnValue = PyObject_CallObject(pyFunc, pyArgs);
	if (PyErr_Occurred() != NULL) { PyErr_Print(); PyErr_Clear(); PYTHON_UNLOCK(); return false; }
	
	bool returnValue;
	
	if (!PyTuple_Check(pyReturnValue)) {
		returnValue = PyBool_AsBool(pyReturnValue);
	} else {
		returnValue = PyBool_AsBool(PyTuple_GetItem(pyReturnValue, 0));
		
		// Are we offloading the file handling to Kodi?
		if (returnValue && PyTuple_Size(pyReturnValue) > 1) {
			// We are!
			char* fileName = PyString_SafeAsString(PyTuple_GetItem(pyReturnValue, 1));
			
			streamHandle = XBMC->OpenFile(fileName, 0);
			
			if (!streamHandle) {
				XBMC->Log(LOG_DEBUG, "%s - Failed to open stream natively", __FUNCTION__);
				returnValue = false;
			} else {
				XBMC->Log(LOG_DEBUG, "%s - Opened stream natively", __FUNCTION__);
				returnValue = true;
			}
		}
	}
	
	Py_DECREF(pyReturnValue);
	Py_DECREF(pyArgs);
	Py_DECREF(pyFunc);
	
	PYTHON_UNLOCK();
	
	return returnValue;
}

int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) {
	//MAYBE_LOG_CALL(); // This gets called a lot.
	
	if (!streamHandle) {
		PYTHON_LOCK();
		
		PyObject* pyReturnValue = pyCall(pvrImpl, "ReadLiveStream", Py_BuildValue("(i)", iBufferSize));
		int bytesRead = PyInt_AsLong(PyTuple_GetItem(pyReturnValue, 0));
		
		if (bytesRead > 0) {
			char* contents = PyString_SafeAsString(PyTuple_GetItem(pyReturnValue, 1));
			memcpy(pBuffer, contents, bytesRead);
		}
		
		Py_DECREF(pyReturnValue);
		
		PYTHON_UNLOCK();
		
		return bytesRead;
	} else {
		return XBMC->ReadFile(streamHandle, pBuffer, iBufferSize);
	}
}

long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) {
	MAYBE_LOG_CALL();
	
	if (!streamHandle) {
		return pyLockCallInt(pvrImpl, "SeekLiveStream", Py_BuildValue("(i, i)", iPosition, iWhence));
	} else {
		return XBMC->SeekFile(streamHandle, iPosition, iWhence);
	}
}

long long PositionLiveStream(void) {
	MAYBE_LOG_CALL();
	
	if (!streamHandle) {
		return pyLockCallInt(pvrImpl, "PositionLiveStream", NULL);
	} else {
		return XBMC->GetFilePosition(streamHandle);
	}
}

long long LengthLiveStream(void) {
	MAYBE_LOG_CALL();
	
	if (!streamHandle) {
		return pyLockCallInt(pvrImpl, "LengthLiveStream", NULL);
	} else {
		return XBMC->GetFileLength(streamHandle);
	}
}

void CloseLiveStream(void)
{
	MAYBE_LOG_CALL();
	
	if (!streamHandle) {
		pyLockCall(pvrImpl, "CloseLiveStream", NULL);
	} else {
		XBMC->CloseFile(streamHandle);
		streamHandle = NULL;
	}
}

bool CanPauseStream(void) {
	//MAYBE_LOG_CALL(); // Lots of calls.
	return pyLockCallBool(pvrImpl, "CanPauseStream", NULL);
}

// Apparently the pause button only works if we can also seek.
bool CanSeekStream(void) {
	//MAYBE_LOG_CALL(); // Lots of calls.
	return pyLockCallBool(pvrImpl, "CanSeekStream", NULL);
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
	//MAYBE_LOG_CALL(); // This gets called a lot.
	
	strcpy(signalStatus.strAdapterStatus, "OK");
	
	return PVR_ERROR_NO_ERROR;
	
	//snprintf(signalStatus.strAdapterName, sizeof(signalStatus.strAdapterName), "pvr demo adapter 1");
	//snprintf(signalStatus.strAdapterStatus, sizeof(signalStatus.strAdapterStatus), "OK");
	//
	//return PVR_ERROR_NO_ERROR;
}

bool SwitchChannel(const PVR_CHANNEL &channel)
{
	MAYBE_LOG_NYI();
	
	CloseLiveStream();
	
	return OpenLiveStream(channel);
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES* pProperties)
{
	MAYBE_LOG_NYI();
	return PVR_ERROR_NOT_IMPLEMENTED;
}

int GetChannelGroupsAmount(void)
{
	MAYBE_LOG_NYI();
	return -1;
	
	//if (m_data)
	//	return m_data->GetChannelGroupsAmount();
	//
	//return -1;
}

/** UNUSED API FUNCTIONS */
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR MoveChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool OpenRecordedStream(const PVR_RECORDING &recording) { return false; }
void CloseRecordedStream(void) {}
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */) { return 0; }
long long PositionRecordedStream(void) { return -1; }
long long LengthRecordedStream(void) { return 0; }
void DemuxReset(void) {}
void DemuxFlush(void) {}
const char * GetLiveStreamURL(const PVR_CHANNEL &channel) { MAYBE_LOG_NYI(); return ""; }
PVR_ERROR DeleteRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) { return -1; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
PVR_ERROR AddTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR UpdateTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
unsigned int GetChannelSwitchDelay(void) { return 0; }
void PauseStream(bool bPaused) { MAYBE_LOG_NYI(); } // This seemingly never actually gets called.
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) { MAYBE_LOG_NYI(); };
bool IsTimeshifting(void) { return false; }
bool IsRealTimeStream(void) { return true; }
time_t GetPlayingTime() { return 0; }
time_t GetBufferTimeStart() { return 0; }
time_t GetBufferTimeEnd() { return 0; }
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
}
