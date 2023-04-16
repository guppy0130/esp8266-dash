FROM python:slim

WORKDIR /app
ENV PYTHONPATH=/app

EXPOSE 8000

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y git \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

COPY pyproject.toml ./
COPY src ./src

# mount in local .git for setuptools_scm:
# https://github.com/pypa/setuptools_scm/issues/77#issuecomment-844927695
RUN --mount=source=.git,target=.git,type=bind \
  pip install --no-cache-dir --no-color .

CMD [ "hypercorn", "api:app", "--bind", "0.0.0.0:8000" ]
