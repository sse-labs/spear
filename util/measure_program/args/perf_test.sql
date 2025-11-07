-- =======================================
-- SQLite Medium Performance Benchmark
-- =======================================

PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

DROP TABLE IF EXISTS customers;
DROP TABLE IF EXISTS orders;

-- Schema
CREATE TABLE customers (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    city TEXT
);

CREATE TABLE orders (
    id INTEGER PRIMARY KEY,
    customer_id INTEGER,
    amount REAL,
    order_date TEXT,
    FOREIGN KEY(customer_id) REFERENCES customers(id)
);

-- Indexes
CREATE INDEX idx_orders_customer ON orders(customer_id);
CREATE INDEX idx_orders_amount ON orders(amount);

-- Data load
WITH RECURSIVE cnt(x) AS (
  SELECT 1
  UNION ALL
  SELECT x+1 FROM cnt WHERE x < 25000
)
INSERT INTO customers (name, age, city)
SELECT 'Customer_' || x,
       (ABS(RANDOM()) % 70) + 18,
       'City_' || (ABS(RANDOM()) % 200)
FROM cnt;

WITH RECURSIVE cnt(x) AS (
  SELECT 1
  UNION ALL
  SELECT x+1 FROM cnt WHERE x < 150000
)
INSERT INTO orders (customer_id, amount, order_date)
SELECT (ABS(RANDOM()) % 25000) + 1,
       ROUND((ABS(RANDOM()) % 200000) / 100.0, 2),
       date('now', '-' || (ABS(RANDOM()) % 400) || ' days')
FROM cnt;

-- Test queries
SELECT COUNT(*) FROM customers;
SELECT * FROM customers WHERE id = 12345;
SELECT COUNT(*) FROM orders WHERE amount BETWEEN 50 AND 200;

SELECT COUNT(*) FROM customers WHERE city LIKE 'City_15%';

SELECT c.name, o.amount, o.order_date
FROM customers c
JOIN orders o ON c.id = o.customer_id
WHERE o.amount > 300
ORDER BY o.amount DESC
LIMIT 20;

SELECT city, AVG(age), COUNT(*) AS cnt
FROM customers
GROUP BY city
ORDER BY cnt DESC
LIMIT 10;

SELECT c.city, SUM(o.amount) AS total_spent
FROM customers c
JOIN orders o ON c.id = o.customer_id
GROUP BY c.city
ORDER BY total_spent DESC
LIMIT 10;

-- Light write tests
BEGIN TRANSACTION;
INSERT INTO orders (customer_id, amount, order_date)
VALUES (9999, 88.88, date('now'));
INSERT INTO orders (customer_id, amount, order_date)
VALUES (9999, 99.99, date('now'));
COMMIT;

UPDATE customers SET city = 'City_New' WHERE id % 5 = 0;
DELETE FROM orders WHERE amount < 1;

VACUUM;
