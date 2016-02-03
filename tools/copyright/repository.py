#  vim: set fileencoding=utf-8 :
#
#  Copyright (C) 2011 Guido Guenther <agx@sigxcpu.org>
#  Copyright (C) 2016 Intel Corporation
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
"""
Someone who modifiers something in git

like committing changes or authoring a patch
"""
import re
import select
import os.path
import subprocess
import collections
import calendar, datetime

class GitModifierError(Exception):
    """Exception thrown by L{GitModifier}"""
    pass

class GitRepositoryError(Exception):
    """Exception thrown by L{GitRepository}"""
    pass

class GitArgs(object):
    def __init__(self, *args):
        self._args = []
        self.add(args)

    @property
    def args(self):
        return self._args

    def add(self, *args):
        """
        Add arguments to argument list
        """
        for arg in args:
            if isinstance(arg, basestring):
                self._args.append(arg)
            elif isinstance(arg, collections.Iterable):
                for i in iter(arg):
                    self._args.append(str(i))
            else:
                self._args.append(str(arg))

        return self
    def add_cond(self, condition, opt, noopt=[]):
        """
        Add option I{opt} to I{alist} if I{condition} is C{True}
        else add I{noopt}.

        @param condition: condition
        @type condition: C{bool}
        @param opt: option to add if I{condition} is C{True}
        @type opt: C{str} or C{list}
        @param noopt: option to add if I{condition} is C{False}
        @type noopt: C{str} or C{list}
        """
        if condition:
            self.add(opt)
        else:
            self.add(noopt)
        return self


class GitTz(datetime.tzinfo):
    """Simple class to store the utc offset only"""
    def __init__(self, offset_sec=0, *args, **kwargs):
        super(GitTz, self).__init__(*args, **kwargs)
        self._offset = datetime.timedelta(seconds=offset_sec)

    def utcoffset(self, dt):
        return self._offset

    def dst(self, dt):
        return datetime.timedelta(0)

class GitModifier(object):
    """Stores authorship/committer information"""
    def __init__(self, name=None, email=None, date=None):
        """
        @param name: the modifier's name
        @type name: C{str}
        @param email: the modifier's email
        @type email: C{str}
        @param date: the date of the modification
        @type date: C{str} (git raw date), C{int} (timestamp) or I{datetime} object
        """
        self.name = name
        self.email = email
        self._parse_date(date)

    def _parse_date(self, date):
        self._date = None
        tz = GitTz(0)

        if isinstance(date, basestring):
            timestamp, offset = date.split()
            offset_h = int(offset[:-2])
            offset_m = int(offset[-2:])
            tz = GitTz(offset_h*3600 + offset_m*60)
            self._date = datetime.datetime.fromtimestamp(int(timestamp), tz)
        elif type(date) in  [ type(0), type(0.0) ]:
            self._date = datetime.datetime.fromtimestamp(date, tz)
        elif isinstance(date, datetime.datetime):
            if date.tzinfo:
                self._date = date
            else:
                self._date = date.replace(tzinfo=tz)
        elif date != None:
            raise ValueError("Date '%s' not timestamp, "
                             "datetime object or git raw date" % date)
    def get_date(self):
        """Return date as a git raw date"""
        if self._date:
            return "%s %s" % (calendar.timegm(self._date.utctimetuple()),
                              self._date.strftime('%z'))
        else:
            return None

    def set_date(self, date):
        """Set date from timestamp, git raw date or datetime object"""
        self._parse_date(date)

    date = property(get_date, set_date)

    @property
    def datetime(self):
        """Return the date as datetime object"""
        return self._date

    @property
    def tz_offset(self):
        """Return the date's UTC offset"""
        return self._date.strftime('%z')

class GitRepository(object):
    """
    Represents a git repository at I{path}. It's currently assumed that the git
    repository is stored in a directory named I{.git/} below I{path}.

    @ivar _path: The path to the working tree
    @type _path: C{str}
    @raises GitRepositoryError: on git errors GitRepositoryError is raised by
        all methods.
    """

    def _check_dirs(self):
        """Get top level dir and git meta data dir"""
        out, dummy, ret = self._git_inout('rev-parse', ['--git-dir'],
                                      capture_stderr=True)
        if ret:
            raise GitRepositoryError(
                "Failed to get repository git dir at '%s'" % self.path)

        # Set git meta data dir
        git_dir = out.strip()
        if os.path.isabs(git_dir):
            self._git_dir = git_dir
        else:
            self._git_dir = os.path.abspath(os.path.join(self.path, git_dir))

        # Set top level dir correctly (in case repo was initialized
        # from a subdir, for example)
        if self.bare:
            self._path = self._git_dir
        else:
            out, dummy, ret = self._git_inout('rev-parse', ['--show-toplevel'],
                                              capture_stderr=True)
            self._path = os.path.abspath(out.strip())

    def __init__(self, path):
        self._path = os.path.abspath(path)
        try:
            # Check for bare repository
            out, dummy, ret = self._git_inout('rev-parse', ['--is-bare-repository'],
                                              capture_stderr=True)
            if ret:
                raise GitRepositoryError("No Git repository at '%s': '%s'" % (self.path, out))
            self._bare = False if out.strip() != 'true' else True

            self._check_dirs()

        except GitRepositoryError:
            raise # We already have a useful error message
        except:
            raise GitRepositoryError("No Git repository at '%s' (or any parent dir)" % self.path)


    @staticmethod
    def __build_env(extra_env):
        """Prepare environment for subprocess calls"""
        env = None
        if extra_env is not None:
            env = os.environ.copy()
            env.update(extra_env)
        return env

    def _git_getoutput(self, command, args=[], extra_env=None, cwd=None):
        """
        Run a git command and return the output

        @param command: git command to run
        @type command: C{str}
        @param args: list of arguments
        @type args: C{list}
        @param extra_env: extra environment variables to pass
        @type extra_env: C{dict}
        @param cwd: directory to swith to when running the command, defaults to I{self.path}
        @type cwd: C{str}
        @return: stdout, return code
        @rtype: C{tuple} of C{list} of C{str} and C{int}

        @deprecated: use L{gbp.git.repository.GitRepository._git_inout} instead.
        """
        output = []

        if not cwd:
            cwd = self.path

        env = self.__build_env(extra_env)
        cmd = ['git', command] + args
        popen = subprocess.Popen(cmd, stdout=subprocess.PIPE, env=env, cwd=cwd)
        while popen.poll() == None:
            output += popen.stdout.readlines()
        output += popen.stdout.readlines()
        return output, popen.returncode

    def _git_inout(self, command, args, input=None, extra_env=None, cwd=None,
                   capture_stderr=False, capture_stdout=True):
        """
        Run a git command with input and return output

        @param command: git command to run
        @type command: C{str}
        @param input: input to pipe to command
        @type input: C{str}
        @param args: list of arguments
        @type args: C{list}
        @param extra_env: extra environment variables to pass
        @type extra_env: C{dict}
        @param capture_stderr: whether to capture stderr
        @type capture_stderr: C{bool}
        @return: stdout, stderr, return code
        @rtype: C{tuple} of C{str}, C{str}, C{int}
        """
        if not cwd:
            cwd = self.path
        ret = 0
        stdout = ''
        stderr = ''
        try:
            for outdata in self.__git_inout(command, args, input, extra_env,
                                            cwd, capture_stderr,
                                            capture_stdout):
                stdout += outdata[0]
                stderr += outdata[1]
        except GitRepositoryError as err:
            ret = err.returncode
        return stdout, stderr, ret

    @classmethod
    def __git_inout(cls, command, args, stdin, extra_env, cwd, capture_stderr,
                    capture_stdout):
        """
        Run a git command without a a GitRepostitory instance.

        Returns the git command output (stdout, stderr) as a Python generator
        object.

        @note: The caller must consume the iterator that is returned, in order
        to make sure that the git command runs and terminates.
        """
        def rm_polled_fd(file_obj, select_list):
            file_obj.close()
            select_list.remove(file_obj)

        cmd = ['git', command] + args
        env = cls.__build_env(extra_env)
        stdout_arg = subprocess.PIPE if capture_stdout else None
        stdin_arg = subprocess.PIPE if stdin else None
        stderr_arg = subprocess.PIPE if capture_stderr else None

        popen = subprocess.Popen(cmd,
                                 stdin=stdin_arg,
                                 stdout=stdout_arg,
                                 stderr=stderr_arg,
                                 env=env,
                                 close_fds=True,
                                 cwd=cwd)
        out_fds = [popen.stdout] if capture_stdout else []
        if capture_stderr:
            out_fds.append(popen.stderr)
        in_fds = [popen.stdin] if stdin else []
        w_ind = 0
        while out_fds or in_fds:
            ready = select.select(out_fds, in_fds, [])
            # Write in chunks of 512 bytes
            if ready[1]:
                try:
                    popen.stdin.write(stdin[w_ind:w_ind+512])
                except IOError:
                    # Ignore, we want to read buffers to e.g. get error message
                    # Git should give an error code so that we catch an error
                    pass
                w_ind += 512
                if w_ind > len(stdin):
                    rm_polled_fd(popen.stdin, in_fds)
            # Read in chunks of 4k
            stdout = popen.stdout.read(4096) if popen.stdout in ready[0] else ''
            stderr = popen.stderr.read(4096) if popen.stderr in ready[0] else ''
            if popen.stdout in ready[0] and not stdout:
                rm_polled_fd(popen.stdout, out_fds)
            if popen.stderr in ready[0] and not stderr:
                rm_polled_fd(popen.stderr, out_fds)
            yield stdout, stderr

        if popen.wait():
            err = GitRepositoryError('git-%s failed' % command)
            err.returncode = popen.returncode
            raise err

    @property
    def path(self):
        """The absolute path to the repository"""
        return self._path

    @property
    def bare(self):
        """Wheter this is a bare repository"""
        return self._bare

    @property
    def git_dir(self):
        """The absolute path to git's metadata"""
        return os.path.join(self.path, self._git_dir)

    def rev_parse(self, name, short=0):
        """
        Find the SHA1 of a given name

        @param name: the name to look for
        @type name: C{str}
        @param short:  try to abbreviate SHA1 to given length
        @type short: C{int}
        @return: the name's sha1
        @rtype: C{str}
        """
        args = GitArgs("--quiet", "--verify")
        args.add_cond(short, '--short=%d' % short)
        args.add(name)
        sha, stderr, ret = self._git_inout('rev-parse', args.args,
                                            capture_stderr=True)
        if ret:
            raise GitRepositoryError("revision '%s' not found" % name)
        return self.strip_sha1(sha.splitlines()[0], short)

    @staticmethod
    def strip_sha1(sha1, length=0):
        """
        Strip a given sha1 and check if the resulting
        hash has the expected length.
         """
        maxlen = 40
        s = sha1.strip()

        l = length or maxlen

        if len(s) < l or len(s) > maxlen:
            raise GitRepositoryError("'%s' is not a valid sha1%s" %
                                     (s, " of length %d" % l if length else ""))
        return s

    def get_config(self, name):
        """
        Gets the config value associated with I{name}

        @param name: config value to get
        @return: fetched config value
        @rtype: C{str}
        """
        value, ret = self._git_getoutput('config', [ name ])
        if ret: raise KeyError
        return value[0][:-1] # first line with \n ending removed

    def get_author_info(self):
        """
        Determine a sane values for author name and author email from git's
        config and environment variables.

        @return: name and email
        @rtype: L{GitModifier}
        """
        try:
           name =  self.get_config("user.name")
        except KeyError:
           name = os.getenv("USER")
        try:
           email =  self.get_config("user.email")
        except KeyError:
            email = os.getenv("EMAIL")
        email = os.getenv("GIT_AUTHOR_EMAIL", email)
        name = os.getenv("GIT_AUTHOR_NAME", name)
        return GitModifier(name, email)

    def get_commit_info(self, commitish):
        """
        Look up data of a specific commit-ish. Dereferences given commit-ish
        to the commit it points to.

        @param commitish: the commit-ish to inspect
        @return: the commit's including id, author, email, subject and body
        @rtype: dict
        """
        commit_sha1 = self.rev_parse("%s^0" % commitish)
        args = GitArgs('--pretty=format:%an%x00%ae%x00%ad%x00%cn%x00%ce%x00%cd%x00%s%x00%f%x00%b%x00',
                       '-z', '--date=raw', '--no-renames', '--name-status',
                       commit_sha1)
        out, err, ret =  self._git_inout('show', args.args)
        if ret:
            raise GitRepositoryError("Unable to retrieve commit info for %s"
                                     % commitish)

        fields = out.split('\x00')

        author = GitModifier(fields[0].strip(),
                             fields[1].strip(),
                             fields[2].strip())
        committer = GitModifier(fields[3].strip(),
                                fields[4].strip(),
                                fields[5].strip())

        files = collections.defaultdict(list)
        file_fields = fields[9:]
        # For some reason git returns one extra empty field for merge commits
        if file_fields[0] == '': file_fields.pop(0)
        while len(file_fields) and file_fields[0] != '':
            status = file_fields.pop(0).strip()
            path = file_fields.pop(0)
            files[status].append(path)

        return {'id' : commitish,
                'author' : author,
                'committer' : committer,
                'files' : files}
