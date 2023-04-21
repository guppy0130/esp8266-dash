from collections import defaultdict
from datetime import datetime, timezone, tzinfo
from importlib.metadata import distribution
from io import BytesIO
from typing import List

import seaborn
from fastapi import FastAPI, Response
from fastapi.responses import PlainTextResponse
from matplotlib.axes import Axes
from matplotlib.dates import DateFormatter
from matplotlib import pyplot
from noaa_sdk import NOAA
from pydantic import parse_obj_as
from PIL import Image

from weather_models import NOAAForecastModel

pyplot.switch_backend("Agg")

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


def _get_hourly_forecast(lat: float, lon: float):
    response = noaa.points_forecast(lat=lat, long=lon, type="forecastHourly")
    forecasts = parse_obj_as(
        List[NOAAForecastModel],
        response["properties"]["periods"],
    )
    # sometimes, NOAA might reply with a forecast in the past.
    return sorted(
        filter(
            lambda forecast: forecast.start_time >= datetime.now(timezone.utc),
            forecasts,
        ),
        key=lambda forecast: forecast.start_time,
    )


@app.get("/forecast/{lat},{lon}", response_class=PlainTextResponse)
def get_hourly_forecast(lat: float, lon: float) -> str:
    """
    Gets the hourly forecast for a lat,lon and returns a string adequate for
    directly displaying on some display.

    You should customize the number of hourly forecasts returned, etc.
    """

    # we only want the first 4 forecasts so that it fits on the display.
    serialized_forecasts = [
        str(forecast).split("\n")
        for forecast in _get_hourly_forecast(lat=lat, lon=lon)
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


@app.get(
    "/forecast-img/{lat},{lon}",
    responses={200: {"content": {"image/bmp": {}}}},
    response_class=Response,
)
def get_hourly_forecast_image(lat: float, lon: float):
    """
    Returns a 400x200x1 bitmap for 7 days.
    """
    forecast = _get_hourly_forecast(lat=lat, lon=lon)

    # restructure data
    data = defaultdict(list)
    # next 24 hours only?
    max_hours = 24
    for idx, f in enumerate(forecast):
        if idx >= max_hours:
            break
        for k, v in f.dict().items():
            data[k].append(v)
    # this should always be present from NOAA reply
    _tz: tzinfo = data["start_time"][0].tzinfo

    # generate plot + graph results
    seaborn.set_palette(["black"], 1)
    axes: Axes = seaborn.lineplot(
        data=data,
        x="start_time",
        y="temperature",
    )

    # show min/max
    min_temp_idx = data["temperature"].index(min(data["temperature"]))
    axes.vlines(
        x=data["start_time"][min_temp_idx],
        ymin=min(data["temperature"]) - 1,
        ymax=min(data["temperature"]),
        linestyles="dashed",
    )
    axes.text(
        data["start_time"][min_temp_idx],
        0.03,
        f" {min(data['temperature'])}",
        # ha="center",
        transform=axes.get_xaxis_transform(),
    )
    max_temp_idx = data["temperature"].index(max(data["temperature"]))
    axes.vlines(
        x=data["start_time"][max_temp_idx],
        ymin=min(data["temperature"]) - 1,
        ymax=max(data["temperature"]),
        linestyles="dashed",
    )
    axes.text(
        data["start_time"][max_temp_idx],
        0.03,
        f" {max(data['temperature'])}",
        # ha="center",
        transform=axes.get_xaxis_transform(),
    )

    # hide labels, set title
    axes.set(xlabel=None, ylabel=None, title="Temperature")
    axes.xaxis.set_major_formatter(DateFormatter("%H", tz=_tz))

    # matplotlib won't let us save to bmp directly, so save to intermediate
    # format, then read in intermediate format and use pillow to convert to
    # 1-depth bmp
    with BytesIO() as png_buf, BytesIO() as bmp_buf:
        fig = axes.get_figure()
        # set the image dimensions here
        fig.set_size_inches(4, 2)
        fig.set_dpi(100)
        fig.savefig(
            png_buf,
            transparent=True,
            format="png",
            pil_kwargs={"optimize": True},
        )
        fig.clear()
        with Image.open(png_buf) as im:
            im.convert("1", colors=2).save(
                bmp_buf, format="bmp", optimize=True
            )

        return Response(content=bmp_buf.getvalue(), media_type="image/bmp")
