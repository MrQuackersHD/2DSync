from __future__ import annotations

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from .models import Base
from .settings import Settings


def make_engine(settings: Settings):
    kwargs = {}
    if settings.database_url.startswith("sqlite"):
        kwargs["connect_args"] = {"check_same_thread": False}
    return create_engine(settings.database_url, pool_pre_ping=True, **kwargs)


def make_session_factory(engine):
    return sessionmaker(bind=engine, expire_on_commit=False, autoflush=False)


def init_database(engine) -> None:
    Base.metadata.create_all(engine)
