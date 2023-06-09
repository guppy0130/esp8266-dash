from datetime import datetime
from enum import Enum
from typing import Any, Optional

from pydantic import BaseModel, HttpUrl

from utils import pascal_generator


class NOAAUnitEnum(str, Enum):
    """Converts between `wmoUnit`-prefixed NOAA responses to representation"""

    percent = "wmoUnit:percent"
    degC = "wmoUnit:degC"
    degF = "wmoUnit:degF"

    def __str__(self) -> str:
        enum_lut = {
            NOAAUnitEnum.percent: "%",
            NOAAUnitEnum.degC: "C",
            NOAAUnitEnum.degF: "F",
        }
        return f"{enum_lut[self]}"


class ValueWithUnit(BaseModel):
    """A value with some `wmoUnit`-prefixed unit from NOAA"""

    class Config:
        allow_population_by_field_name = True
        alias_generator = pascal_generator

    unit_code: NOAAUnitEnum
    # truncate to int to save space
    value: int

    def __str__(self) -> str:
        enum_lut = {
            NOAAUnitEnum.percent: "%",
            NOAAUnitEnum.degC: "C",
            NOAAUnitEnum.degF: "F",
        }
        return f"{self.value}{enum_lut[self.unit_code]}"


class NOAAForecastModel(BaseModel):
    """
    A forecast entry, with a __str__() to serialize it into something for small
    displays
    """

    class Config:
        allow_population_by_field_name = True
        alias_generator = pascal_generator

    number: int
    name: str
    start_time: datetime
    end_time: datetime
    is_daytime: bool
    temperature: int
    temperature_unit: str
    temperature_trend: Optional[Any]
    probability_of_precipitation: ValueWithUnit
    dewpoint: ValueWithUnit
    relative_humidity: ValueWithUnit
    wind_speed: str
    wind_direction: str
    icon: HttpUrl
    short_forecast: str
    detailed_forecast: str

    def __str__(self) -> str:
        return "\n".join(
            [
                f"{self.temperature}{self.temperature_unit}",
                # `Mostly Sunny` -> `Sunny`
                f"{self.short_forecast.split()[-1]}",
                "",
                f"{self.relative_humidity} hum",
                f"{self.probability_of_precipitation} pre",
                # removing extra spaces in the wind speed
                f"{''.join(self.wind_speed.split())}",
                # we want non-zero-prefixed hours, so %I is insufficient
                f"{self.start_time.hour % 12}-{self.end_time.hour % 12}"
                f"{self.start_time.strftime('%p')}",
            ]
        )
