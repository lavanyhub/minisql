-- Demo script exercising every feature of the engine.
CREATE TABLE users (id INT, name TEXT, age INT, dept INT);
INSERT INTO users VALUES (1, Alice, 30, 10);
INSERT INTO users VALUES (2, Bob, 25, 20);
INSERT INTO users VALUES (3, Carol, 40, 10);
INSERT INTO users VALUES (4, Dave, 35, 20);
INSERT INTO users VALUES (5, Eve, 28, 10);

CREATE TABLE depts (did INT, dname TEXT);
INSERT INTO depts VALUES (10, Engineering);
INSERT INTO depts VALUES (20, Sales);

SELECT * FROM users;
SELECT name, age FROM users WHERE age > 30;
SELECT * FROM users WHERE age >= 28 ORDER BY age DESC;
SELECT dept, COUNT(*) FROM users GROUP BY dept;
SELECT dept, AVG(age) FROM users GROUP BY dept HAVING > 30;
SELECT * FROM users INNER JOIN depts ON dept = did;
UPDATE users SET age = 41 WHERE name = Carol;
SELECT name, age FROM users WHERE name = Carol;
DELETE FROM users WHERE age < 28;
SELECT * FROM users;
