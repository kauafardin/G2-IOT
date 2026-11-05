-- Uma linha por leitura. ts (epoch em segundos) é a chave: reenvio do mesmo lote não duplica.
CREATE TABLE IF NOT EXISTS leituras (
  ts         INTEGER PRIMARY KEY,
  temp_c     REAL    NOT NULL,
  umid_pct   REAL    NOT NULL,
  ventilador INTEGER NOT NULL CHECK (ventilador IN (0, 1))
);
