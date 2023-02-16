import numpy as np
from influxdb import InfluxDBClient
import datetime
import pandas as pd
import os
host = ‘REDACTED’
port = ‘REDACTED’
read_db = ‘REDACTED’
post_db = ‘REDACTED’
user_admin, password_admin = ‘REDACTED’,‘REDACTED’
user_read, password_read = ‘REDACTED’,‘REDACTED’
client_admin = InfluxDBClient(host= host, port= port, username= user_admin,
               password = password_admin, database = read_db)
client_user = InfluxDBClient(host= host, port= port, username= user_read,
               password = password_read, database = read_db)
def run_query(client, field, measurement, tags, pagesize=10000, read_db = read_db):
  ### Pull data off influx via InfluxDBClient ###
  collect = []
  times = []
  values = []
  q = True
  pagenum = 0
  # Single quotes around tags might not always work
  tag_str = ' AND ‘.join([“{key}=‘{value}’“.format(key=key, value=value) for key, value
              in tags.items()])
  client.switch_database(read_db)
  while q:
    q = client.query((“SELECT {field} FROM {measurement} WHERE {tags} ”
             “LIMIT {pagesize} OFFSET {page}“)
             .format(field=field, measurement=measurement, tags=tag_str,
             pagesize=pagesize, page=pagenum*pagesize))
    if q:
      collect.append(q[measurement])
    pagenum += 1
  for resultset in collect:
    for reading in resultset:
      times.append(reading[‘time’])
      values.append(reading[field])
  s = pd.Series(values, index=times)
  s.index = pd.to_datetime(s.index)
  return s
def bounded_time_query(client, field, measurement, tags, t_start, t_end,
          pagesize=10000, read_db = read_db):
  ### Pull data off influx via InfluxDBClient ###
  # t_start and t_end are datetimes formatted like “%Y-%m-%dT%H:%M:%S.000000000Z”
  collect = []
  times = []
  values = []
  q = True
  pagenum = 0
  # Single quotes around tags might not always work
  tag_str = ' AND ‘.join([“{key}=‘{value}‘“.format(key=key, value=value) for key, value in tags.items()])+   “AND time >= ‘” + t_start + “’ AND time <= ‘” + t_end + “’”
  client.switch_database(read_db)
  while q:
    q_string = (“SELECT {field} FROM {measurement} WHERE {tags} ”
             “LIMIT {pagesize} OFFSET {page}“).format(
            field=field,
            measurement=measurement,
            tags=tag_str,
            pagesize=pagesize,
            page=pagenum*pagesize)
    q = client.query(q_string)
    if q:
      collect.append(q[measurement])
    pagenum += 1
  for resultset in collect:
    for reading in resultset:
      times.append(reading[‘time’])
      values.append(reading[field])
  s = pd.Series(values, index=times)
  s.index = pd.to_datetime(s.index)
  return s
