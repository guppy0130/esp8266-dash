from datetime import datetime
from datetime import timezone
from importlib.metadata import distribution
from typing import List

from fastapi import FastAPI
from fastapi.responses import PlainTextResponse
from noaa_sdk import NOAA
from pydantic import parse_obj_as

from weather_models import NOAAForecastModel

app_distribution = distribution("esp8266-dash-server").metadata

app = FastAPI(
    title=app_distribution["name"],
    version=app_distribution["version"],
    description=app_distribution["summary"],
)
noaa = NOAA()


@app.get("/")
def get_root():
    return {"status": "ok"}


@app.get("/forecast/{lat},{lon}", response_class=PlainTextResponse)
def get_hourly_forecast(lat: float, lon: float) -> str:
    """
    Gets the hourly forecast for a lat,lon and returns a string adequate for
    directly displaying on some display.

    You should customize the number of hourly forecasts returned, etc.
    """
    response = noaa.points_forecast(lat=lat, long=lon, type="forecastHourly")
    forecasts = parse_obj_as(
        List[NOAAForecastModel],
        response["properties"]["periods"],
    )

    # sometimes, NOAA might reply with a forecast in the past. also, we only
    # want the first 4 forecasts so that it fits on the display.
    serialized_forecasts = [
        str(forecast).split("\n")
        for forecast in forecasts
        if forecast.start_time >= datetime.now(timezone.utc)
    ][:4]

    # determine longest str
    longest_str_len = 0
    for f in serialized_forecasts:
        longest_len_in_f = max(len(f_i) for f_i in f)
        if longest_len_in_f > longest_str_len:
            longest_str_len = longest_len_in_f

    # horizontally join the returned strs
    for i, f in enumerate(serialized_forecasts):
        for j, text in enumerate(f):
            # center every value relative to its cell
            serialized_forecasts[i][j] = text.center(longest_str_len)
    return "\n".join("|".join(f) for f in zip(*serialized_forecasts))
