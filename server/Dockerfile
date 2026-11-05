FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY app ./app

RUN useradd --create-home --uid 10001 twodsync \
    && mkdir -p /data/saves \
    && chown -R twodsync:twodsync /data /app
USER twodsync

ENV SAVE_STORAGE_DIR=/data/saves
EXPOSE 8000
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8000"]
