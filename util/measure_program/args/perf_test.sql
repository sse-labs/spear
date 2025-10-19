-- ===============================
-- SQLite Performance Test Script
-- ===============================

-- Drop old tables if they exist
DROP TABLE IF EXISTS customers;
DROP TABLE IF EXISTS orders;

-- 1. Schema setup
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

-- 2. Index creation
CREATE INDEX idx_orders_customer ON orders(customer_id);

-- 3. Data population
-- NOTE: SQLite does not have generate_series(), so we simulate with a recursive CTE.
-- Adjust limits (e.g. 10000 customers, 50000 orders) to scale dataset size.

WITH RECURSIVE cnt(x) AS (
  SELECT 1
  UNION ALL
  SELECT x+1 FROM cnt WHERE x < 10000
)
INSERT INTO customers (name, age, city)
SELECT 'Customer_' || x,
       (ABS(RANDOM()) % 70) + 18,
       'City_' || (ABS(RANDOM()) % 100)
FROM cnt;

WITH RECURSIVE cnt(x) AS (
  SELECT 1
  UNION ALL
  SELECT x+1 FROM cnt WHERE x < 50000
)
INSERT INTO orders (customer_id, amount, order_date)
SELECT (ABS(RANDOM()) % 10000) + 1,
       ROUND(RANDOM() % 10000 / 100.0, 2),
       date('now', '-' || (ABS(RANDOM()) % 365) || ' days')
FROM cnt;

-- 4. Test queries

-- a) Full table scan
SELECT COUNT(*) AS total_customers FROM customers;

-- b) Simple indexed lookup
SELECT * FROM customers WHERE id = 1234;

-- c) Range query on numeric column
SELECT COUNT(*) AS mid_price_orders FROM orders WHERE amount BETWEEN 50 AND 100;

-- d) String filtering
SELECT COUNT(*) AS customers_city5 FROM customers WHERE city LIKE 'City_5%';

-- e) Join query
SELECT c.name, o.amount, o.order_date
FROM customers c
JOIN orders o ON c.id = o.customer_id
WHERE o.amount > 500
ORDER BY o.amount DESC
LIMIT 10;

-- f) Aggregation
SELECT city, AVG(age) AS avg_age, COUNT(*) AS cnt
FROM customers
GROUP BY city
ORDER BY cnt DESC
LIMIT 10;

-- g) Aggregation with join
SELECT c.city, SUM(o.amount) AS total_spent
FROM customers c
JOIN orders o ON c.id = o.customer_id
GROUP BY c.city
ORDER BY total_spent DESC
LIMIT 10;

-- h) Transactions + batch insert
BEGIN TRANSACTION;
INSERT INTO orders (customer_id, amount, order_date)
VALUES (9999, 123.45, '2024-01-01');
INSERT INTO orders (customer_id, amount, order_date)
VALUES (9999, 234.56, '2024-02-01');
INSERT INTO orders (customer_id, amount, order_date)
VALUES (9999, 345.67, '2024-03-01');
COMMIT;

-- i) Update benchmark
UPDATE customers SET city = 'City_Updated' WHERE id % 2 = 0;

-- j) Delete benchmark
DELETE FROM orders WHERE amount < 10;

-- 5. Vacuum / cleanup timing
VACUUM;
