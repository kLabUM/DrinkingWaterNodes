from UROP_influxClient_code import *
import os
from datetime import datetime, timedelta
import pytz
import time
import sys
import requests
from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError
slack_token = “REDACTED”
def slack_message_api(slack_channel, slack_message):
  slack_client = WebClient(token=slack_token)
  try:
    response = slack_client.chat_postMessage(
      channel= slack_channel,
      text=slack_message
    )
  except SlackApiError as e:
    # You will get a SlackApiError if “ok” is False
    assert e.response[“error”]  # str like ‘invalid_auth’, ‘channel_not_found’
def writeto_node(node_id):
  devices = {“NODE1”:“DEVICE_ID”,
        “NODE2”:“DEVICE_ID”,
        “NODE3”:“DEVICE_ID”,
        “NODE4”:“DEVICE_ID”,
        “NODE5”:“DEVICE_ID”,
        “NODE6”:“DEVICE_ID”,
        "NODE7":"DEVICE_ID"} 
  device = devices[node_id]
  thresholds = {“NODE1”:350,
        “NODE2”:400,
        “NODE3”:340,
        “NODE4”:400,
        “NODE5”:305,
        “NODE6”:350,
        "NODE7":700}
  threshold = thresholds[node_id]
  url = f”https://api.particle.io/v1/devices/{device}/Smart%20Flush”
  t_end = datetime.strftime(datetime.now(pytz.utc), “%Y-%m-%dT%H:%M:%S.000000000Z”)
  t_start = datetime.strftime(datetime.now(pytz.utc) - timedelta(minutes = 60), “%Y-%m-%dT%H:%M:%S.000000000Z”)
  s = bounded_time_query(client_user, field=‘value’, measurement=‘Temp’, tags={‘node_id’ : node_id},
                t_start=t_start, t_end=t_end)
  df = pd.DataFrame({‘Temp’ : s})
#  df.index = df.index.floor(‘min’) #Rounds datetime index to the nearest minute.
  s = bounded_time_query(client_user, field=‘value’, measurement=‘ORP’, tags={‘node_id’ : node_id},
                t_start=t_start, t_end=t_end)
  s = pd.DataFrame({‘ORP’ : s})
#  s.index = s.index.floor(‘min’)
#  df = s.copy()
  df = pd.concat([df, s], axis=1) #concatenate Temp and ORP into same df using the same index.
  df = df.assign(ORP_SHE = df[‘ORP’] + 225 - 262.55 + 1.5111*df[‘Temp’]) #Convert ORP_Ag-AgCl to the SHE
  df = df.assign(ORP_roll = df[‘ORP_SHE’].rolling(3).median()) #median filter applied to the ORP_she column
  df = df.assign(ORP_diff6 = df[‘ORP_roll’].diff(periods=6)) #apply difference within the same column 6 rows apart.
  if len(df) > 0:
    if ((df[‘ORP_diff6’]< -20).any()): #if a drop in the ORP signal greater than 20mV occurs then flush.
      data = {“arg”:“HIGH3", “access_token”:“REDACTED”}
      r = requests.post(url, data = data)
      print(r)
      slack_message_api(slack_channel=“qaqc”, slack_message= f”<@userid> SMART FLUSHY: ORP for {node_id} is wildin :(“)
    elif ((df[‘ORP_roll’][-1] < threshold).any()): #else if any value is below the ORP signal threshold then flush.
      data = {“arg”:“HIGH3", “access_token”:“REDACTED”}
      r = requests.post(url, data = data)
      print(r)
    else:
      print(“did not update device!“)
  else:
    print(“no data”)
              
  #This function may be expanded to smart STOP flushing by measuring temperature or ORP in realtime. Develop and implement here. 
  #Argument would be as data = {“arg”:“LOW", “access_token”:“REDACTED”}

writeto_node(“NODE1”)
