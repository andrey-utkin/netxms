/**
 * 
 */
package org.netxms.agent.android.service;

import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.Calendar;

import org.netxms.agent.android.R;
import org.netxms.agent.android.helpers.SafeParser;
import org.netxms.agent.android.receivers.AlarmIntentReceiver;
import org.netxms.agent.android.service.helpers.AndroidLoggingFacility;
import org.netxms.agent.android.service.helpers.LocationManagerHelper;
import org.netxms.base.GeoLocation;
import org.netxms.base.Logger;
import org.netxms.mobile.agent.MobileAgentException;
import org.netxms.mobile.agent.Session;

import android.annotation.SuppressLint;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.location.Criteria;
import android.location.Location;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.AsyncTask;
import android.os.BatteryManager;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.preference.PreferenceManager;
import android.telephony.TelephonyManager;
import android.util.Log;
import android.widget.Toast;

/**
 * Background communication service for NetXMS agent.
 * 
 * @author Marco Incalcaterra (marco.incalcaterra@thinksoft.it)
 * 
 */

public class ClientConnectorService extends Service
{
	public enum ConnectionStatus
	{
		CS_NOCONNECTION, CS_INPROGRESS, CS_ALREADYCONNECTED, CS_CONNECTED, CS_DISCONNECTED, CS_ERROR
	};

	public static final String ACTION_CONNECT = "org.netxms.ui.android.ACTION_CONNECT";
	public static final String ACTION_FORCE_CONNECT = "org.netxms.ui.android.ACTION_FORCE_CONNECT";
	public static final String ACTION_RESCHEDULE = "org.netxms.ui.android.ACTION_RESCHEDULE";
	private static final String TAG = "nxagent/ClientConnectorService";
	private static final String LASTALARM_KEY = "LastALarmIdNotified";

	private static final int ONE_DAY_MINUTES = 24 * 60;
	private static final int NETXMS_REQUEST_CODE = 123456;

	private final String mutex = "MUTEX";
	private final Binder binder = new ClientConnectorBinder();
	private Handler uiThreadHandler;
	private final ConnectionStatus connectionStatus = ConnectionStatus.CS_DISCONNECTED;
	private long lastAlarmIdNotified;
	private BroadcastReceiver receiver = null;
	private SharedPreferences sp;

	/**
	 * Class for clients to access. Because we know this service always runs in
	 * the same process as its clients, we don't need to deal with IPC.
	 */
	public class ClientConnectorBinder extends Binder
	{
		public ClientConnectorService getService()
		{
			return ClientConnectorService.this;
		}
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onCreate()
	 */
	@Override
	public void onCreate()
	{
		super.onCreate();

		Logger.setLoggingFacility(new AndroidLoggingFacility());
		uiThreadHandler = new Handler(getMainLooper());

		showToast(getString(R.string.notify_started));

		sp = PreferenceManager.getDefaultSharedPreferences(this);
		lastAlarmIdNotified = sp.getInt(LASTALARM_KEY, 0);

		receiver = new BroadcastReceiver()
		{
			@Override
			public void onReceive(Context context, Intent intent)
			{
				Intent i = new Intent(context, ClientConnectorService.class);
				i.setAction(ACTION_RESCHEDULE);
				context.startService(i);
			}
		};
		registerReceiver(receiver, new IntentFilter(Intent.ACTION_TIME_TICK));
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onStartCommand(android.content.Intent, int, int)
	 */
	@Override
	public int onStartCommand(Intent intent, int flags, int startId)
	{
		if ((intent != null) && (intent.getAction() != null))
			if (intent.getAction().equals(ACTION_CONNECT))
				reconnect(false);
			else if (intent.getAction().equals(ACTION_FORCE_CONNECT))
				reconnect(true);
			else if (intent.getAction().equals(ACTION_RESCHEDULE))
				reconnect(false);
		return super.onStartCommand(intent, flags, startId);
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onBind(android.content.Intent)
	 */
	@Override
	public IBinder onBind(Intent intent)
	{
		return binder;
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onDestroy()
	 */
	@Override
	public void onDestroy()
	{
		super.onDestroy();
	}

	/**
	 * Shutdown background service
	 */
	public void shutdown()
	{
		cancelSchedule();
		savePreferences();
		unregisterReceiver(receiver);
		stopSelf();
	}

	/**
	 * 
	 */
	public void savePreferences()
	{
		SharedPreferences.Editor editor = sp.edit();
		editor.putInt(LASTALARM_KEY, (int)lastAlarmIdNotified);
		editor.commit();
	}

	/**
	 * Show status notification
	 * 
	 * @param status	connection status
	 * @param extra	extra text to add at the end of the toast
	 */
	public void statusNotification(ConnectionStatus status, String extra)
	{
		String text = "";
		switch (status)
		{
			case CS_CONNECTED:
				text = getString(R.string.notify_connected);
				break;
			case CS_ERROR:
				text = getString(R.string.notify_connection_failed, extra);
				break;
			case CS_NOCONNECTION:
			case CS_INPROGRESS:
			case CS_ALREADYCONNECTED:
			default:
				return;
		}
		if (sp.getBoolean("global.notification.toast", true))
			showToast(text);
	}

	/**
	 * Reconnect to server.
	 * 
	 * @param forceReconnect if set to true forces disconnection before connecting
	 */
	public void reconnect(boolean force)
	{
		if (force || (isScheduleExpired()) &&
				connectionStatus != ConnectionStatus.CS_CONNECTED &&
				connectionStatus != ConnectionStatus.CS_ALREADYCONNECTED)
		{
			Log.i(TAG, "Reconnecting...");
			synchronized (mutex)
			{
				if (connectionStatus != ConnectionStatus.CS_INPROGRESS)
				{
					statusNotification(ConnectionStatus.CS_INPROGRESS, "");
					new PushDataTask(
							sp.getString("connection.server", ""),
							SafeParser.parseInt(sp.getString("connection.port", "4747"), 4747),
							getImei(),
							sp.getString("connection.login", ""),
							sp.getString("connection.password", ""),
							sp.getBoolean("connection.encrypt", false)).execute();
				}
			}
		}
	}

	/**
	 * Check for expired pending connection schedule
	 */
	private boolean isScheduleExpired()
	{
		if (sp.getBoolean("global.scheduler.enable", false))
		{
			Calendar cal = Calendar.getInstance(); // get a Calendar object with current time
			return cal.getTimeInMillis() > sp.getLong("global.scheduler.next_activation", 0);
		}
		return true;
	}

	/**
	 * Gets stored time settings in minutes
	 */
	private int getMinutes(String time)
	{
		String[] vals = sp.getString(time, "00:00").split(":");
		return Integer.parseInt(vals[0]) * 60 + Integer.parseInt(vals[1]);
	}

	/**
	 * Sets the offset used to compute the next schedule
	 */
	private void setDayOffset(Calendar cal, int minutes)
	{
		cal.set(Calendar.HOUR_OF_DAY, 0);
		cal.set(Calendar.MINUTE, 0);
		cal.set(Calendar.SECOND, 0);
		cal.set(Calendar.MILLISECOND, 0);
		cal.add(Calendar.MINUTE, minutes);
	}

	/**
	 * Schedule a new connection/disconnection
	 */
	public void schedule(String action)
	{
		Log.i(TAG, "Schedule: " + action);
		if (!sp.getBoolean("global.scheduler.enable", false))
			cancelSchedule();
		else
		{
			Calendar cal = Calendar.getInstance(); // get a Calendar object with current time
			if (action == ACTION_RESCHEDULE)
				cal.add(Calendar.MINUTE, Integer.parseInt(sp.getString("global.scheduler.postpone", "1")));
			else if (!sp.getBoolean("global.scheduler.daily.enable", false))
				cal.add(Calendar.MINUTE, Integer.parseInt(sp.getString("global.scheduler.interval", "15")));
			else
			{
				int on = getMinutes("global.scheduler.daily.on");
				int off = getMinutes("global.scheduler.daily.off");
				if (off < on)
					off += ONE_DAY_MINUTES; // Next day!
				Calendar calOn = (Calendar)cal.clone();
				setDayOffset(calOn, on);
				Calendar calOff = (Calendar)cal.clone();
				setDayOffset(calOff, off);
				cal.add(Calendar.MINUTE, Integer.parseInt(sp.getString("global.scheduler.interval", "15")));
				if (cal.before(calOn))
				{
					cal = (Calendar)calOn.clone();
					Log.i(TAG, "schedule (before): rescheduled for daily interval");
				}
				else if (cal.after(calOff))
				{
					cal = (Calendar)calOn.clone();
					setDayOffset(cal, on + ONE_DAY_MINUTES); // Move to the next activation of the excluded range
					Log.i(TAG, "schedule (after): rescheduled for daily interval");
				}
			}
			setSchedule(cal.getTimeInMillis(), action);
		}
	}

	/**
	 * Set a connection schedule
	 */
	private void setSchedule(long milliseconds, String action)
	{
		Intent intent = new Intent(this, AlarmIntentReceiver.class);
		intent.putExtra("action", action);
		PendingIntent sender = PendingIntent.getBroadcast(this, NETXMS_REQUEST_CODE, intent, PendingIntent.FLAG_UPDATE_CURRENT);
		((AlarmManager)getSystemService(ALARM_SERVICE)).set(AlarmManager.RTC_WAKEUP, milliseconds, sender);
		// Update status
		long last = sp.getLong("global.scheduler.next_activation", 0);
		Editor e = sp.edit();
		e.putLong("global.scheduler.last_activation", last);
		e.putLong("global.scheduler.next_activation", milliseconds);
		e.commit();
	}

	/**
	 * Cancel a pending connection schedule (if any)
	 */
	public void cancelSchedule()
	{
		Intent intent = new Intent(this, AlarmIntentReceiver.class);
		PendingIntent sender = PendingIntent.getBroadcast(this, NETXMS_REQUEST_CODE, intent, PendingIntent.FLAG_UPDATE_CURRENT);
		((AlarmManager)getSystemService(ALARM_SERVICE)).cancel(sender);
		if (sp.getLong("global.scheduler.next_activation", 0) != 0)
		{
			Editor e = sp.edit();
			e.putLong("global.scheduler.next_activation", 0);
			e.commit();
		}
	}

	@SuppressWarnings("deprecation")
	public String getNextConnectionRetry()
	{
		long next = sp.getLong("global.scheduler.next_activation", 0);
		if (next != 0)
		{
			Calendar cal = Calendar.getInstance(); // get a Calendar object with current time
			if (cal.getTimeInMillis() < next)
			{
				cal.setTimeInMillis(next);
				return " " + getString(R.string.notify_next_connection_schedule, cal.getTime().toLocaleString());
			}
		}
		return "";
	}

	/**
	 * Show toast with given text
	 * 
	 * @param text message text
	 */
	public void showToast(final String text)
	{
		uiThreadHandler.post(new Runnable()
		{
			@Override
			public void run()
			{
				Toast.makeText(getApplicationContext(), text, Toast.LENGTH_SHORT).show();
			}
		});
	}

	/**
	 * Get IMEI number
	 */
	private String getImei()
	{
		TelephonyManager tman = (TelephonyManager)getSystemService(Context.TELEPHONY_SERVICE);
		return tman != null ? tman.getDeviceId() : "NO-IMEI";
	}

	/**
	 * Get OS nickname
	 */
	private String getOSName(int sdk)
	{
		switch (sdk)
		{
			case Build.VERSION_CODES.BASE:
				return "ANDROID (BASE)";
			case Build.VERSION_CODES.BASE_1_1:
				return "ANDROID (BASE_1_1)";
			case Build.VERSION_CODES.CUR_DEVELOPMENT:
				return "ANDROID (CUR_DEVELOPMENT)";
			case Build.VERSION_CODES.DONUT:
				return "ANDROID (DONUT)";
			case Build.VERSION_CODES.ECLAIR:
				return "ANDROID (ECLAIR)";
			case Build.VERSION_CODES.ECLAIR_0_1:
				return "ANDROID (ECLAIR_0_1)";
			case Build.VERSION_CODES.ECLAIR_MR1:
				return "ANDROID (ECLAIR_MR1)";
			case Build.VERSION_CODES.FROYO:
				return "ANDROID (FROYO)";
			case Build.VERSION_CODES.GINGERBREAD:
				return "ANDROID (GINGERBREAD)";
			case Build.VERSION_CODES.GINGERBREAD_MR1:
				return "ANDROID (GINGERBREAD_MR1)";
			case Build.VERSION_CODES.HONEYCOMB:
				return "ANDROID (HONEYCOMB)";
			case Build.VERSION_CODES.HONEYCOMB_MR1:
				return "ANDROID (HONEYCOMB_MR1)";
			case Build.VERSION_CODES.HONEYCOMB_MR2:
				return "ANDROID (HONEYCOMB_MR2)";
			case Build.VERSION_CODES.ICE_CREAM_SANDWICH:
				return "ANDROID (ICE_CREAM_SANDWICH)";
			case Build.VERSION_CODES.ICE_CREAM_SANDWICH_MR1:
				return "ANDROID (ICE_CREAM_SANDWICH_MR1)";
			case Build.VERSION_CODES.JELLY_BEAN:
				return "ANDROID (JELLY_BEAN)";
			case Build.VERSION_CODES.JELLY_BEAN_MR1:
				return "ANDROID (JELLY_BEAN_MR1)";
		}
		return "ANDROID (UNKNOWN)";
	}

	/**
	 * Get internet address. NB must run on background thread (as per honeycomb specs)
	 */
	private InetAddress getInetAddress()
	{
		try
		{
			return InetAddress.getLocalHost();
		}
		catch (UnknownHostException e)
		{
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		return null;
	}

	/**
	 * Get geo location. NB must rub on a UI thread.
	 */
	private GeoLocation getGeoLocation()
	{
		final LocationManager locationManager = (LocationManager)getSystemService(Context.LOCATION_SERVICE);
		final LocationManagerHelper lmh = new LocationManagerHelper();

		String mlocProvider;
		Criteria hdCrit = new Criteria();

		hdCrit.setAccuracy(Criteria.ACCURACY_COARSE);
		mlocProvider = locationManager.getBestProvider(hdCrit, true);
		try
		{
			locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 3000, 1000, lmh);
		}
		catch (IllegalArgumentException e)
		{
			e.printStackTrace();
		}
		catch (RuntimeException e)
		{
			e.printStackTrace();
		}
		Location currLocation = locationManager.getLastKnownLocation(mlocProvider);
		locationManager.removeUpdates(lmh);
		if (currLocation != null)
			return new GeoLocation(currLocation.getLatitude(), currLocation.getLongitude());
		return new GeoLocation(0, 0);
	}

	/**
	 * Get flags. Currently not used.
	 */
	private int getFlags()
	{
		return 0;
	}

	/**
	 * Get battery level as percentage. -1 if value is not available
	 */
	private int getBatteryLevel()
	{
		double level = -1;
		Context context = getApplicationContext();
		if (context != null)
		{
			Intent battIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
			if (battIntent != null)
			{
				int rawlevel = battIntent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
				double scale = battIntent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
//				int temp = battIntent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1);
//	            int voltage = battIntent.getIntExtra(BatteryManager.EXTRA_VOLTAGE, -1);
				if (rawlevel >= 0 && scale > 0)
					level = (rawlevel * 100) / scale + 0.5; // + 0.5 for round on cast
			}
		}
		return (int)level;
	}

	/**
	 * Check for internet connectivity 
	 */
	private boolean isInternetOn()
	{
		ConnectivityManager conn = (ConnectivityManager)getSystemService(Context.CONNECTIVITY_SERVICE);
		if (conn != null)
		{
			NetworkInfo wifi = conn.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
			if (wifi != null && wifi.getState() == NetworkInfo.State.CONNECTED)
				return true;
			NetworkInfo mobile = conn.getNetworkInfo(ConnectivityManager.TYPE_MOBILE);
			if (mobile != null && mobile.getState() == NetworkInfo.State.CONNECTED)
				return true;
		}
		return false;
	}

	/**
	 * Internal task for connecting and pushing data
	 */
	private class PushDataTask extends AsyncTask<Object, Void, Boolean>
	{
		private final String server;
		private final Integer port;
		private final String deviceId;
		private final String login;
		private final String password;
		private final boolean encrypt;
		private String error = "";
		private Session session = null;
		private InetAddress inetAddress = null;

		protected PushDataTask(String server, Integer port, String deviceId, String login, String password, boolean encrypt)
		{
			this.server = server;
			this.port = port;
			this.deviceId = deviceId;
			this.login = login;
			this.password = password;
			this.encrypt = encrypt;
		}

		@Override
		protected Boolean doInBackground(Object... params)
		{
			if (isInternetOn())
			{
				session = new Session(server, port, deviceId, login, password, encrypt);
				try
				{
					Log.d(TAG, "PushDataTask.doInBackground: calling session.connect()");
					if (session != null)
					{
						session.connect();
						statusNotification(ConnectionStatus.CS_CONNECTED, "");
						inetAddress = getInetAddress(); // Cannot get it on UI thread (as per honeycomb constraint)
						return true;
					}
				}
				catch (IOException e)
				{
					Log.e(TAG, "IOException while executing PushDataTask.doInBackground on connect", e);
					error = e.getLocalizedMessage();
				}
				catch (MobileAgentException e)
				{
					Log.e(TAG, "MobileAgentException while executing PushDataTask.doInBackground on connect", e);
					error = e.getLocalizedMessage();
				}
			}
			else
			{
				Log.d(TAG, "PushDataTask.doInBackground: no internet connection");
			}
			return false;
		}

		@SuppressLint("NewApi")
		@Override
		protected void onPostExecute(Boolean result)
		{
			if (result == true)
			{
				try
				{
					String serial = "";
					if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB)
						serial = Build.SERIAL;
					session.reportDeviceSystemInfo(Build.MANUFACTURER, Build.MODEL, getOSName(Build.VERSION.SDK_INT), Build.VERSION.RELEASE, serial, Build.USER);
					session.reportDeviceStatus(inetAddress, getGeoLocation(), getFlags(), getBatteryLevel()); // getGeoLocation must run on UI thread
				}
				catch (IOException e)
				{
					Log.e(TAG, "IOException while executing PushDataTask.doInBackground on connect", e);
				}
				catch (MobileAgentException e)
				{
					Log.e(TAG, "MobileAgentException while executing PushDataTask.doInBackground on connect", e);
				}
				session.disconnect();
				Log.i(TAG, "Disconnected.");
				statusNotification(ConnectionStatus.CS_DISCONNECTED, "");
				schedule(ACTION_RESCHEDULE);
			}
			else
			{
				statusNotification(ConnectionStatus.CS_ERROR, error);
			}
		}
	}
}
